#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "kh/model/Commands.h"
#include "kh/model/Document.h"

using namespace kh;
using namespace kh::model;

namespace {

std::unique_ptr<BoxFeature> box(const std::string& name, kernel::Vec3 size = {10, 10, 10}) {
    return std::make_unique<BoxFeature>(name, kernel::Vec3{0, 0, 0}, size);
}

FeatureId addBox(Document& doc, const std::string& name) {
    auto f = box(name);
    const auto id = f->id();
    REQUIRE(doc.execute(std::make_unique<AddFeatureCommand>(std::move(f))).empty());
    return id;
}

FeatureId addCut(Document& doc, const std::string& name, FeatureId a, FeatureId b) {
    auto f = std::make_unique<BooleanFeature>(name, kernel::BoolOp::Cut, a, b);
    const auto id = f->id();
    REQUIRE(doc.execute(std::make_unique<AddFeatureCommand>(std::move(f))).empty());
    return id;
}

}  // namespace

TEST_CASE("Add, undo and redo a feature", "[model][commands]") {
    Document doc;
    REQUIRE_FALSE(doc.canUndo());
    REQUIRE(doc.undoLabel().empty());

    const auto id = addBox(doc, "Box1");
    REQUIRE(doc.featureCount() == 1);
    REQUIRE(doc.find(id));
    REQUIRE(doc.undoLabel() == "Add Box1");

    REQUIRE(doc.undo());
    REQUIRE(doc.featureCount() == 0);
    REQUIRE(doc.redoLabel() == "Add Box1");
    REQUIRE_FALSE(doc.undo());

    REQUIRE(doc.redo());
    REQUIRE(doc.featureCount() == 1);
    REQUIRE(doc.featureAt(0).id() == id);  // same id after redo
    REQUIRE_FALSE(doc.redo());
}

TEST_CASE("A new command clears redo", "[model][commands]") {
    Document doc;
    addBox(doc, "Box1");
    REQUIRE(doc.undo());
    REQUIRE(doc.canRedo());
    addBox(doc, "Box2");
    REQUIRE_FALSE(doc.canRedo());
}

TEST_CASE("A failed command changes nothing and is not in history", "[model][commands]") {
    Document doc;
    addBox(doc, "Box1");

    auto issues = doc.execute(std::make_unique<AddFeatureCommand>(box("Box1")));
    REQUIRE(issues.size() == 1);
    REQUIRE(issues[0].code == "feature.name_taken");
    REQUIRE(doc.featureCount() == 1);
    REQUIRE(doc.undoLabel() == "Add Box1");

    issues = doc.execute(std::make_unique<AddFeatureCommand>(box("")));
    REQUIRE(issues[0].code == "feature.name_empty");

    REQUIRE(doc.execute(nullptr)[0].code == "command.missing");
}

TEST_CASE("A step can only use earlier steps", "[model][commands]") {
    Document doc;
    const auto a = addBox(doc, "Box1");
    const auto b = addBox(doc, "Box2");

    // Insert the cut at the front, before the boxes it uses.
    auto cut = std::make_unique<BooleanFeature>("Cut1", kernel::BoolOp::Cut, a, b);
    auto issues = doc.execute(std::make_unique<AddFeatureCommand>(std::move(cut), 0));
    REQUIRE(issues.size() == 1);
    REQUIRE(issues[0].code == "feature.input_order");

    // An input that does not exist at all.
    auto ghost = std::make_unique<BooleanFeature>("Cut1", kernel::BoolOp::Cut, a, FeatureId::generate());
    issues = doc.execute(std::make_unique<AddFeatureCommand>(std::move(ghost)));
    REQUIRE(issues[0].code == "feature.input_missing");
    REQUIRE(doc.featureCount() == 2);
}

TEST_CASE("Delete is refused while another step uses the feature", "[model][commands]") {
    Document doc;
    const auto a = addBox(doc, "Box1");
    const auto b = addBox(doc, "Box2");
    const auto c = addCut(doc, "Cut1", a, b);

    auto issues = doc.execute(std::make_unique<DeleteFeatureCommand>(a));
    REQUIRE(issues.size() == 1);
    REQUIRE(issues[0].code == "feature.in_use");
    REQUIRE(issues[0].message == "Box1 is used by Cut1. Delete or change Cut1 first.");
    REQUIRE(doc.featureCount() == 3);

    // Deleting the user first, then the input, works.
    REQUIRE(doc.execute(std::make_unique<DeleteFeatureCommand>(c)).empty());
    REQUIRE(doc.execute(std::make_unique<DeleteFeatureCommand>(a)).empty());
    REQUIRE(doc.featureCount() == 1);
}

TEST_CASE("Undoing a delete restores the same id at the same position", "[model][commands]") {
    Document doc;
    addBox(doc, "Box1");
    const auto middle = addBox(doc, "Box2");
    addBox(doc, "Box3");

    REQUIRE(doc.execute(std::make_unique<DeleteFeatureCommand>(middle)).empty());
    REQUIRE(doc.undoLabel() == "Delete Box2");
    REQUIRE(doc.featureCount() == 2);
    REQUIRE_FALSE(doc.find(middle));

    REQUIRE(doc.undo());
    REQUIRE(doc.featureCount() == 3);
    REQUIRE(doc.featureAt(1).id() == middle);
    REQUIRE(doc.featureAt(1).name() == "Box2");

    REQUIRE(doc.redo());
    REQUIRE_FALSE(doc.find(middle));
}

TEST_CASE("Edit a feature, then undo the edit", "[model][commands]") {
    Document doc;
    const auto id = addBox(doc, "Box1");

    auto copy = doc.find(id)->clone();
    static_cast<BoxFeature&>(*copy).setSize({20, 30, 40});
    REQUIRE(doc.execute(std::make_unique<EditFeatureCommand>(std::move(copy))).empty());
    REQUIRE(doc.undoLabel() == "Edit Box1");

    const auto& edited = static_cast<const BoxFeature&>(doc.featureAt(0));
    REQUIRE(edited.id() == id);
    REQUIRE(edited.size().x == 20);

    REQUIRE(doc.undo());
    REQUIRE(static_cast<const BoxFeature&>(doc.featureAt(0)).size().x == 10);
    REQUIRE(doc.redo());
    REQUIRE(static_cast<const BoxFeature&>(doc.featureAt(0)).size().x == 20);
}

TEST_CASE("An edit cannot change the kind of step", "[model][commands]") {
    Document doc;
    const auto id = addBox(doc, "Box1");

    // A different kind of feature that copies the box's id (and name).
    struct Impostor final : Feature {
        explicit Impostor(const Feature& f) : Feature(f) {}
        FeatureType type() const override { return FeatureType::Cylinder; }
        std::unique_ptr<Feature> clone() const override { return std::make_unique<Impostor>(*this); }
        kernel::OpResult execute(kernel::IKernel&, const ResultLookup&) const override { return {}; }
    };
    auto issues =
        doc.execute(std::make_unique<EditFeatureCommand>(std::make_unique<Impostor>(*doc.find(id))));
    REQUIRE(issues.size() == 1);
    REQUIRE(issues[0].code == "feature.type_changed");
    REQUIRE(doc.find(id)->type() == FeatureType::Box);

    // An edit of a step that is not in the design.
    issues = doc.execute(std::make_unique<EditFeatureCommand>(box("Box9")));
    REQUIRE(issues[0].code == "feature.not_found");
}

TEST_CASE("Continuous edits merge into one undo step; separate edits do not", "[model][commands]") {
    Document doc;
    const auto id = addBox(doc, "Box1");

    auto setWidth = [&](double w, bool continues) {
        auto copy = doc.find(id)->clone();
        static_cast<BoxFeature&>(*copy).setSize({w, 10, 10});
        REQUIRE(doc.execute(std::make_unique<EditFeatureCommand>(std::move(copy), continues)).empty());
    };
    auto width = [&] { return static_cast<const BoxFeature&>(*doc.find(id)).size().x; };

    SECTION("dragging a value") {
        setWidth(11, false);
        setWidth(12, true);
        setWidth(13, true);
        REQUIRE(width() == 13);
        REQUIRE(doc.undo());  // one step goes back to before the drag
        REQUIRE(width() == 10);
        REQUIRE(doc.undoLabel() == "Add Box1");
        REQUIRE(doc.redo());
        REQUIRE(width() == 13);
    }
    SECTION("typing two separate values") {
        setWidth(11, false);
        setWidth(12, false);
        REQUIRE(doc.undo());
        REQUIRE(width() == 11);
        REQUIRE(doc.undo());
        REQUIRE(width() == 10);
    }
}

TEST_CASE("Rename keeps names unique and undoes", "[model][commands]") {
    Document doc;
    const auto a = addBox(doc, "Box1");
    addBox(doc, "Box2");

    auto issues = doc.execute(std::make_unique<RenameFeatureCommand>(a, "Box2"));
    REQUIRE(issues[0].code == "feature.name_taken");
    issues = doc.execute(std::make_unique<RenameFeatureCommand>(a, ""));
    REQUIRE(issues[0].code == "feature.name_empty");

    REQUIRE(doc.execute(std::make_unique<RenameFeatureCommand>(a, "Base")).empty());
    REQUIRE(doc.undoLabel() == "Rename Box1 to Base");
    REQUIRE(doc.find(a)->name() == "Base");
    REQUIRE(doc.uniqueName("Box") == "Box1");  // Box1 is free again

    REQUIRE(doc.undo());
    REQUIRE(doc.find(a)->name() == "Box1");
}

TEST_CASE("Unique names count up from 1", "[model][document]") {
    Document doc;
    REQUIRE(doc.uniqueName("Box") == "Box1");
    addBox(doc, "Box1");
    addBox(doc, "Box2");
    REQUIRE(doc.uniqueName("Box") == "Box3");
    REQUIRE(doc.uniqueName("Cut") == "Cut1");
}

TEST_CASE("The undo limit drops the oldest steps", "[model][commands]") {
    Document doc;
    doc.setUndoLimit(3);
    for (int i = 1; i <= 5; ++i) addBox(doc, "Box" + std::to_string(i));

    int undos = 0;
    while (doc.undo()) ++undos;
    REQUIRE(undos == 3);
    REQUIRE(doc.featureCount() == 2);  // Box1 and Box2 can no longer be undone
}

TEST_CASE("isModified follows save, undo and redo", "[model][document]") {
    Document doc;
    REQUIRE_FALSE(doc.isModified());

    addBox(doc, "Box1");
    REQUIRE(doc.isModified());
    doc.markSaved();
    REQUIRE_FALSE(doc.isModified());

    addBox(doc, "Box2");
    REQUIRE(doc.isModified());
    REQUIRE(doc.undo());
    REQUIRE_FALSE(doc.isModified());  // back to the saved state
    REQUIRE(doc.undo());
    REQUIRE(doc.isModified());
    REQUIRE(doc.redo());
    REQUIRE_FALSE(doc.isModified());

    // A new change after undo is a different state, even with the same count.
    addBox(doc, "Box3");
    REQUIRE(doc.isModified());
}

TEST_CASE("Ids stay the same through many undo and redo cycles", "[model][commands]") {
    Document doc;
    const auto a = addBox(doc, "Box1");
    const auto b = addBox(doc, "Box2");
    const auto c = addCut(doc, "Cut1", a, b);

    for (int i = 0; i < 5; ++i) {
        while (doc.undo()) {
        }
        REQUIRE(doc.featureCount() == 0);
        while (doc.redo()) {
        }
        REQUIRE(doc.featureCount() == 3);
        REQUIRE(doc.featureAt(0).id() == a);
        REQUIRE(doc.featureAt(1).id() == b);
        REQUIRE(doc.featureAt(2).id() == c);
    }
    REQUIRE(doc.dependents(a) == std::vector<FeatureId>{c});
}
