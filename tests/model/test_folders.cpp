// Tests for folders in the feature tree: AddFolderCommand, RenameFolderCommand,
// DeleteFolderCommand, Document::folders() and the folder.* commands.

#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "FakeKernel.h"
#include "kh/model/CommandRegistry.h"
#include "kh/model/Commands.h"
#include "kh/model/Document.h"

using namespace kh;
using namespace kh::model;

namespace {

FeatureId add(Document& doc, std::unique_ptr<Feature> f) {
    const auto id = f->id();
    REQUIRE(doc.execute(std::make_unique<AddFeatureCommand>(std::move(f))).empty());
    return id;
}

// Plate, Hole, Tag = Plate cut Hole, and Spare.
struct Tag {
    Document doc;
    FeatureId plate =
        add(doc, std::make_unique<BoxFeature>("Plate", kernel::Vec3{0, 0, 0}, kernel::Vec3{60, 30, 4}));
    FeatureId hole = add(doc, std::make_unique<CylinderFeature>("Hole", kernel::Vec3{9, 15, -1},
                                                                kernel::Vec3{0, 0, 1}, 2.5, 6));
    FeatureId tag = add(doc, std::make_unique<BooleanFeature>("Tag", kernel::BoolOp::Cut, plate, hole));
    FeatureId spare =
        add(doc, std::make_unique<BoxFeature>("Spare", kernel::Vec3{100, 0, 0}, kernel::Vec3{1, 2, 3}));

    Diagnostics folder(std::vector<FeatureId> steps, std::string name = {}) {
        return doc.execute(std::make_unique<AddFolderCommand>(std::move(steps), std::move(name)));
    }
};

}  // namespace

TEST_CASE("a new folder holds its steps in design order, and undo takes it away", "[model][folders]") {
    Tag t;
    CHECK(t.doc.folders().empty());
    REQUIRE(t.folder({t.hole, t.plate}, "Parts").empty());  // picked in any order
    CHECK(t.doc.undoLabel() == "New folder Parts");
    const auto folders = t.doc.folders();
    REQUIRE(folders.size() == 1);
    CHECK(folders[0].name == "Parts");
    CHECK(folders[0].steps == std::vector<FeatureId>{t.plate, t.hole});
    CHECK(t.doc.folderOf(t.hole) == folders[0].id);
    CHECK(t.doc.folderOf(t.tag).isNil());
    CHECK(t.doc.findFolder(std::string_view("Parts")) == t.doc.findFolder(folders[0].id));
    CHECK(t.doc.featureCount() == 4);  // folders don't add or move steps

    const FolderId id = folders[0].id;
    REQUIRE(t.doc.undo());
    CHECK(t.doc.folders().empty());
    CHECK(t.doc.folderOf(t.hole).isNil());
    REQUIRE(t.doc.redo());
    REQUIRE(t.doc.folders().size() == 1);
    CHECK(t.doc.folders()[0].id == id);  // same id after redo
}

TEST_CASE("folders don't change what's built", "[model][folders]") {
    Tag t;
    test::FakeKernel k;
    const double before = test::FakeKernel::volumeOf(*t.doc.regenerate(k).shapeOf(t.tag));
    REQUIRE(t.folder({t.plate, t.hole, t.tag}).empty());
    const auto report = t.doc.regenerate(k);
    REQUIRE(report.ok());
    CHECK(test::FakeKernel::volumeOf(*report.shapeOf(t.tag)) == before);
}

TEST_CASE("folders are named Folder1, Folder2, ... unless given a name", "[model][folders]") {
    Tag t;
    REQUIRE(t.folder({t.plate}).empty());
    REQUIRE(t.folder({t.spare}).empty());
    const auto folders = t.doc.folders();
    REQUIRE(folders.size() == 2);
    CHECK(folders[0].name == "Folder1");
    CHECK(folders[1].name == "Folder2");
}

TEST_CASE("a folder's steps must be next to each other and in no other folder", "[model][folders]") {
    Tag t;
    auto problems = t.folder({t.plate, t.tag});  // Hole is between them
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "folder.not_together");
    REQUIRE(t.folder({t.plate, t.hole}, "Parts").empty());
    problems = t.folder({t.hole, t.tag});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "folder.step_taken");
    CHECK(problems[0].message == "Hole is already in the folder Parts. A step can only be in one folder.");
    problems = t.folder({t.spare}, "Parts");
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "folder.name_taken");
    CHECK(t.folder({}).front().code == "folder.nothing");
    CHECK(t.folder({FeatureId::generate()}).front().code == "feature.not_found");
    CHECK(t.doc.folders().size() == 1);
    CHECK(t.doc.undoLabel() == "New folder Parts");
}

TEST_CASE("renaming a folder, and refusing a taken or empty name", "[model][folders]") {
    Tag t;
    REQUIRE(t.folder({t.plate}, "A").empty());
    REQUIRE(t.folder({t.spare}, "B").empty());
    const FolderId a = t.doc.folders()[0].id;
    REQUIRE(t.doc.execute(std::make_unique<RenameFolderCommand>(a, "Base")).empty());
    CHECK(t.doc.undoLabel() == "Rename folder A to Base");
    CHECK(t.doc.findFolder(a)->name == "Base");
    CHECK(t.doc.execute(std::make_unique<RenameFolderCommand>(a, "B")).front().code == "folder.name_taken");
    CHECK(t.doc.execute(std::make_unique<RenameFolderCommand>(a, "")).front().code == "folder.name_empty");
    CHECK(t.doc.execute(std::make_unique<RenameFolderCommand>(FolderId::generate(), "C")).front().code ==
          "folder.not_found");
    REQUIRE(t.doc.undo());
    CHECK(t.doc.findFolder(a)->name == "A");
}

TEST_CASE("removing a folder keeps its steps, and undo puts it back", "[model][folders]") {
    Tag t;
    REQUIRE(t.folder({t.plate}, "A").empty());
    REQUIRE(t.folder({t.hole, t.tag}, "B").empty());
    const auto before = t.doc.folders();
    REQUIRE(t.doc.execute(std::make_unique<DeleteFolderCommand>(before[0].id)).empty());
    CHECK(t.doc.undoLabel() == "Remove folder A");
    CHECK(t.doc.featureCount() == 4);
    REQUIRE(t.doc.folders().size() == 1);
    CHECK(t.doc.folderOf(t.plate).isNil());
    REQUIRE(t.doc.undo());
    CHECK(t.doc.folders() == before);  // same ids, names, steps and order
}

TEST_CASE("a folder whose steps are deleted disappears, and comes back with undo", "[model][folders]") {
    Tag t;
    REQUIRE(t.folder({t.spare}, "Extra").empty());
    REQUIRE(t.folder({t.tag}, "Body").empty());
    REQUIRE(t.doc.execute(std::make_unique<DeleteFeatureCommand>(t.spare)).empty());
    REQUIRE(t.doc.folders().size() == 1);
    CHECK(t.doc.folders()[0].name == "Body");
    CHECK_FALSE(t.doc.findFolder(std::string_view("Extra")));
    REQUIRE(t.folder({t.plate}, "Extra").empty());  // the name is free again
    REQUIRE(t.doc.undo());
    REQUIRE(t.doc.undo());
    REQUIRE(t.doc.folders().size() == 2);
    CHECK(t.doc.folders()[1].name == "Extra");  // listed in tree order: Tag, then Spare
    CHECK(t.doc.folders()[1].steps == std::vector<FeatureId>{t.spare});
}

TEST_CASE("folder.add, folder.rename and folder.delete from the registry", "[model][folders][registry]") {
    Tag t;
    const auto& reg = CommandRegistry::builtin();
    auto made = reg.create(
        "folder.add", {{"steps", std::vector<FeatureId>{t.plate, t.hole}}, {"name", std::string("Parts")}},
        t.doc);
    REQUIRE(made.ok());
    REQUIRE(t.doc.execute(std::move(made.command)).empty());
    const FolderId id = t.doc.folders().at(0).id;

    made = reg.create("folder.rename", {{"folder", id}, {"name", std::string("Tag parts")}}, t.doc);
    REQUIRE(made.ok());
    REQUIRE(t.doc.execute(std::move(made.command)).empty());
    CHECK(t.doc.folders()[0].name == "Tag parts");

    made = reg.create("folder.delete", {{"folder", FolderId::generate()}}, t.doc);
    REQUIRE_FALSE(made.ok());
    CHECK(made.issues[0].code == "command.arg_folder");
    made = reg.create("folder.delete", {{"folder", id}}, t.doc);
    REQUIRE(made.ok());
    REQUIRE(t.doc.execute(std::move(made.command)).empty());
    CHECK(t.doc.folders().empty());
}
