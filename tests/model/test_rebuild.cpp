#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "FakeKernel.h"
#include "kh/model/Commands.h"
#include "kh/model/Document.h"

using namespace kh;
using namespace kh::model;
using kh::test::FakeKernel;

namespace {

template <typename F>
FeatureId add(Document& doc, std::unique_ptr<F> f) {
    const auto id = f->id();
    REQUIRE(doc.execute(std::make_unique<AddFeatureCommand>(std::move(f))).empty());
    return id;
}

FeatureId addBox(Document& doc, const std::string& name, kernel::Vec3 size) {
    return add(doc, std::make_unique<BoxFeature>(name, kernel::Vec3{}, size));
}

void suppress(Document& doc, const FeatureId& id, bool on) {
    auto copy = doc.find(id)->clone();
    copy->setSuppressed(on);
    REQUIRE(doc.execute(std::make_unique<EditFeatureCommand>(std::move(copy))).empty());
}

}  // namespace

TEST_CASE("An empty document rebuilds to nothing", "[model][rebuild]") {
    Document doc;
    FakeKernel k;
    const auto report = doc.regenerate(k);
    REQUIRE(report.ok());
    REQUIRE(report.bodies().empty());
    REQUIRE(k.calls == 0);
}

TEST_CASE("Rebuild runs features in order and finds the final bodies", "[model][rebuild]") {
    Document doc;
    const auto a = addBox(doc, "Box1", {10, 10, 10});
    const auto b = addBox(doc, "Box2", {2, 5, 10});
    const auto c = add(doc, std::make_unique<BooleanFeature>("Cut1", kernel::BoolOp::Cut, a, b));
    const auto d = addBox(doc, "Box3", {1, 1, 1});

    FakeKernel k;
    const auto report = doc.regenerate(k);
    REQUIRE(report.ok());
    REQUIRE(k.calls == 4);
    REQUIRE(FakeKernel::volumeOf(*report.shapeOf(c)) == 1000.0 - 100.0);
    // Box1 and Box2 were used by the cut, so the bodies are Cut1 and Box3.
    REQUIRE(report.bodies() == std::vector<FeatureId>{c, d});
}

TEST_CASE("A failed step blocks the steps that use it", "[model][rebuild]") {
    Document doc;
    const auto a = addBox(doc, "Box1", {10, 10, 0});  // zero height: the kernel fails
    const auto b = addBox(doc, "Box2", {1, 1, 1});
    const auto c = add(doc, std::make_unique<BooleanFeature>("Cut1", kernel::BoolOp::Cut, a, b));
    const auto d = add(doc, std::make_unique<BooleanFeature>("Union1", kernel::BoolOp::Union, c, b));

    FakeKernel k;
    const auto report = doc.regenerate(k);
    REQUIRE_FALSE(report.ok());
    REQUIRE(report.result(a)->status == FeatureStatus::Failed);
    REQUIRE_FALSE(report.result(a)->issues.empty());
    REQUIRE(report.result(b)->status == FeatureStatus::Ok);

    const auto* cut = report.result(c);
    REQUIRE(cut->status == FeatureStatus::Blocked);
    REQUIRE(cut->issues[0].message == "This step uses Box1, which has a problem. Fix that step first.");
    // Blocking passes down the chain.
    REQUIRE(report.result(d)->status == FeatureStatus::Blocked);
    REQUIRE(report.result(d)->issues[0].message ==
            "This step uses Cut1, which has a problem. Fix that step first.");
    REQUIRE(k.calls == 2);  // blocked steps never reach the kernel
    REQUIRE(report.bodies().empty());
}

TEST_CASE("Suppressed steps are skipped and block their users", "[model][rebuild]") {
    Document doc;
    const auto a = addBox(doc, "Box1", {10, 10, 10});
    const auto b = addBox(doc, "Box2", {2, 2, 2});
    const auto c = add(doc, std::make_unique<BooleanFeature>("Cut1", kernel::BoolOp::Cut, a, b));

    suppress(doc, c, true);
    FakeKernel k;
    auto report = doc.regenerate(k);
    REQUIRE(report.ok());  // suppressed is not a problem
    REQUIRE(report.result(c)->status == FeatureStatus::Suppressed);
    REQUIRE_FALSE(report.shapeOf(c));
    // With the cut turned off, both boxes are bodies again.
    REQUIRE(report.bodies() == std::vector<FeatureId>{a, b});

    suppress(doc, c, false);
    suppress(doc, b, true);
    report = doc.regenerate(k);
    REQUIRE(report.result(c)->status == FeatureStatus::Blocked);
    REQUIRE(report.result(c)->issues[0].message ==
            "This step uses Box2, which is turned off. Turn Box2 back on, or change this step.");

    REQUIRE(doc.undo());  // turn Box2 back on
    REQUIRE(doc.regenerate(k).ok());
}

TEST_CASE("Rebuild is repeatable and follows undo", "[model][rebuild]") {
    Document doc;
    const auto a = addBox(doc, "Box1", {10, 10, 10});
    auto copy = doc.find(a)->clone();
    static_cast<BoxFeature&>(*copy).setSize({1, 2, 3});
    REQUIRE(doc.execute(std::make_unique<EditFeatureCommand>(std::move(copy))).empty());

    FakeKernel k;
    REQUIRE(FakeKernel::volumeOf(*doc.regenerate(k).shapeOf(a)) == 6.0);
    REQUIRE(FakeKernel::volumeOf(*doc.regenerate(k).shapeOf(a)) == 6.0);
    REQUIRE(doc.undo());
    REQUIRE(FakeKernel::volumeOf(*doc.regenerate(k).shapeOf(a)) == 1000.0);
}
