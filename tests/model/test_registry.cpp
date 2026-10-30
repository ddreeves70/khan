#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "FakeKernel.h"
#include "kh/model/CommandRegistry.h"
#include "kh/model/Document.h"

using namespace kh;
using namespace kh::model;

namespace {

const CommandRegistry& reg() {
    return CommandRegistry::builtin();
}

bool hasCode(const Diagnostics& issues, const std::string& code) {
    return std::any_of(issues.begin(), issues.end(), [&](const Diagnostic& d) { return d.code == code; });
}

FeatureId run(Document& doc, const std::string& name, const Args& args) {
    auto made = reg().create(name, args, doc);
    REQUIRE(made.ok());
    REQUIRE(made.issues.empty());
    REQUIRE(doc.execute(std::move(made.command)).empty());
    return doc.featureAt(doc.featureCount() - 1).id();
}

}  // namespace

TEST_CASE("The built-in registry lists its commands sorted by name", "[model][registry]") {
    const auto cmds = reg().commands();
    std::vector<std::string> names;
    for (const auto* c : cmds) names.push_back(c->name);
    REQUIRE(names ==
            std::vector<std::string>{
                "document.set_units",  "feature.add_boolean", "feature.add_box",      "feature.add_cylinder",
                "feature.add_extrude", "feature.add_move",    "feature.add_sketch",   "feature.delete",
                "feature.duplicate",   "feature.rename",      "feature.set_material", "feature.suppress",
                "folder.add",          "folder.delete",       "folder.rename",        "material.define",
                "material.delete",     "sketch.add_arc",      "sketch.add_circle",    "sketch.add_line",
                "sketch.add_point",    "sketch.add_spline",   "sketch.constrain",     "sketch.delete",
                "sketch.set_value"});
    for (const auto* c : cmds) {
        REQUIRE_FALSE(c->label.empty());
        REQUIRE_FALSE(c->description.empty());
        for (const auto& a : c->args) REQUIRE_FALSE(a.description.empty());
    }
    REQUIRE(reg().info("feature.add_box")->label == "Add box");
    REQUIRE(reg().info("feature.nope") == nullptr);
}

TEST_CASE("Registry commands build a design that rebuilds", "[model][registry]") {
    Document doc;
    const auto box = run(doc, "feature.add_box", {{"size", kernel::Vec3{10, 10, 10}}});
    const auto cyl = run(doc, "feature.add_cylinder", {{"radius", 2.0}, {"height", std::int64_t{10}}});
    const auto cut =
        run(doc, "feature.add_boolean", {{"operation", std::string("cut")}, {"a", box}, {"b", cyl}});

    REQUIRE(doc.find(box)->name() == "Box1");
    REQUIRE(doc.find(cyl)->name() == "Cylinder1");
    REQUIRE(doc.find(cut)->name() == "Cut1");
    const auto& c = static_cast<const CylinderFeature&>(*doc.find(cyl));
    REQUIRE(c.axis().z == 1.0);   // default axis is up
    REQUIRE(c.height() == 10.0);  // an integer is accepted for a number

    kh::test::FakeKernel k;
    const auto report = doc.regenerate(k);
    REQUIRE(report.ok());
    REQUIRE(kh::test::FakeKernel::volumeOf(*report.shapeOf(cut)) == 1000.0 - 3.0 * 4.0 * 10.0);

    run(doc, "feature.rename", {{"feature", cut}, {"name", std::string("Body")}});
    REQUIRE(doc.find(cut)->name() == "Body");
    run(doc, "feature.delete", {{"feature", cut}});
    REQUIRE(doc.featureCount() == 2);
    REQUIRE(doc.undo());
    REQUIRE(doc.find(cut));
}

TEST_CASE("The registry checks arguments before building a command", "[model][registry]") {
    Document doc;

    SECTION("unknown command") {
        auto r = reg().create("feature.add_sphere", {}, doc);
        REQUIRE_FALSE(r.ok());
        REQUIRE(hasCode(r.issues, "command.unknown"));
    }
    SECTION("missing required argument") {
        auto r = reg().create("feature.add_box", {}, doc);
        REQUIRE_FALSE(r.ok());
        REQUIRE(hasCode(r.issues, "command.arg_missing"));
        REQUIRE(r.issues[0].message == "Add box needs size: Width, depth and height in mm.");
    }
    SECTION("unexpected argument") {
        auto r = reg().create("feature.add_box",
                              {{"size", kernel::Vec3{1, 1, 1}}, {"colour", std::string("red")}}, doc);
        REQUIRE_FALSE(r.ok());
        REQUIRE(hasCode(r.issues, "command.arg_unknown"));
    }
    SECTION("wrong type") {
        auto r = reg().create("feature.add_box", {{"size", 10.0}}, doc);
        REQUIRE_FALSE(r.ok());
        REQUIRE(hasCode(r.issues, "command.arg_type"));
        REQUIRE(r.issues[0].message == "size for Add box must be three numbers (x, y, z).");
    }
    SECTION("choice not in the list") {
        const auto a = run(doc, "feature.add_box", {{"size", kernel::Vec3{1, 1, 1}}});
        auto r = reg().create("feature.add_boolean", {{"operation", std::string("glue")}, {"a", a}, {"b", a}},
                              doc);
        REQUIRE_FALSE(r.ok());
        REQUIRE(hasCode(r.issues, "command.arg_choice"));
        REQUIRE(r.issues[0].message == "operation must be one of: union, cut, intersect.");
    }
    SECTION("feature that is not in the document") {
        auto r = reg().create("feature.delete", {{"feature", FeatureId::generate()}}, doc);
        REQUIRE_FALSE(r.ok());
        REQUIRE(hasCode(r.issues, "command.arg_feature"));
    }
    SECTION("every problem is reported at once") {
        auto r = reg().create("feature.add_cylinder", {{"radius", std::string("big")}}, doc);
        REQUIRE(hasCode(r.issues, "command.arg_type"));
        REQUIRE(hasCode(r.issues, "command.arg_missing"));  // height
    }
    REQUIRE(doc.featureCount() <= 1);
}

TEST_CASE("Commands from the registry still go through the document's own checks", "[model][registry]") {
    Document doc;
    run(doc, "feature.add_box", {{"size", kernel::Vec3{1, 1, 1}}, {"name", std::string("Base")}});
    auto made = reg().create("feature.add_box",
                             {{"size", kernel::Vec3{1, 1, 1}}, {"name", std::string("Base")}}, doc);
    REQUIRE(made.ok());  // the arguments are fine...
    const auto issues = doc.execute(std::move(made.command));
    REQUIRE(hasCode(issues, "feature.name_taken"));  // ...but the name is already used
    REQUIRE(doc.featureCount() == 1);
}
