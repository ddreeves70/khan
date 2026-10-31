// Tests for saving and opening .khan files (kh_io).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <zip.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../model/FakeKernel.h"
#include "kh/core/Version.h"
#include "kh/io/KhanFile.h"
#include "kh/io/Steps.h"
#include "kh/model/Commands.h"
#include "kh/model/Document.h"

using namespace kh;
using Catch::Matchers::WithinAbs;

namespace {

// Builds the demo tag: Plate, Hole, Tag = Plate cut Hole, plus a suppressed Spare box.
std::unique_ptr<model::Document> makeDesign() {
    auto doc = std::make_unique<model::Document>(model::DocKind::Part, "Keychain tag");
    auto plate = std::make_unique<model::BoxFeature>("Plate", kernel::Vec3{0, 0, 0}, kernel::Vec3{60, 30, 4});
    auto hole = std::make_unique<model::CylinderFeature>("Hole", kernel::Vec3{9, 15, -1},
                                                         kernel::Vec3{0, 0, 1}, 2.5, 6);
    auto tag = std::make_unique<model::BooleanFeature>("Tag", kernel::BoolOp::Cut, plate->id(), hole->id());
    auto spare = std::make_unique<model::BoxFeature>("Spare", kernel::Vec3{100, 0, 0}, kernel::Vec3{1, 2, 3});
    spare->setSuppressed(true);
    std::vector<std::unique_ptr<model::Feature>> steps;
    steps.push_back(std::move(plate));
    steps.push_back(std::move(hole));
    steps.push_back(std::move(tag));
    steps.push_back(std::move(spare));
    for (auto& f : steps) {
        REQUIRE(doc->execute(std::make_unique<model::AddFeatureCommand>(std::move(f))).empty());
    }
    return doc;
}

// Writes a zip from name -> text, for hand-made (often broken) files.
io::Bytes makeZip(const std::map<std::string, std::string>& entries) {
    zip_error_t err;
    zip_error_init(&err);
    zip_source_t* src = zip_source_buffer_create(nullptr, 0, 0, &err);
    zip_t* za = zip_open_from_source(src, ZIP_TRUNCATE, &err);
    zip_source_keep(src);
    for (const auto& [name, text] : entries) {
        zip_file_add(za, name.c_str(), zip_source_buffer(za, text.data(), text.size(), 0), ZIP_FL_OVERWRITE);
    }
    zip_close(za);
    zip_source_open(src);
    zip_source_seek(src, 0, SEEK_END);
    const auto size = zip_source_tell(src);
    zip_source_seek(src, 0, SEEK_SET);
    io::Bytes out(static_cast<std::size_t>(size));
    zip_source_read(src, out.data(), out.size());
    zip_source_close(src);
    zip_source_free(src);
    return out;
}

const std::string kDocId = "6f1c1d2e-3a4b-4c5d-8e6f-7a8b9c0d1e2f";
const std::string kBoxId = "0b6e3f5a-1c2d-4e3f-9a4b-5c6d7e8f9a0b";

std::string manifest(int schema = 1) {
    return R"({"format":"khan","schema":)" + std::to_string(schema) +
           R"(,"app":"0.0.1","units":{"length":"mm","angle":"rad"},)"
           R"("document":{"id":")" +
           kDocId + R"(","kind":"part","name":"Hand made"},"files":[]})";
}

std::string timeline(const std::string& id = kBoxId) {
    return R"({"features":[{"id":")" + id + R"(","suppressed":false}]})";
}

std::string boxFeature(const std::string& type = "Box") {
    return R"({"id":")" + kBoxId + R"(","type":")" + type +
           R"(","name":"Box1","params":{"corner":[0,0,0],"size":[10,20,30]}})";
}

// Reads every entry of a zip: name -> text.
std::map<std::string, std::string> readZip(const io::Bytes& bytes) {
    std::map<std::string, std::string> out;
    zip_error_t err;
    zip_error_init(&err);
    zip_source_t* src = zip_source_buffer_create(bytes.data(), bytes.size(), 0, &err);
    zip_t* za = zip_open_from_source(src, ZIP_RDONLY, &err);
    if (!za) {
        zip_source_free(src);
        return out;
    }
    for (zip_int64_t i = 0; i < zip_get_num_entries(za, 0); ++i) {
        zip_stat_t st;
        zip_stat_index(za, static_cast<zip_uint64_t>(i), 0, &st);
        std::string text(static_cast<std::size_t>(st.size), '\0');
        zip_file_t* f = zip_fopen_index(za, static_cast<zip_uint64_t>(i), 0);
        zip_fread(f, text.data(), st.size);
        zip_fclose(f);
        out[st.name] = text;
    }
    zip_close(za);
    return out;
}

std::string firstCode(const io::OpenResult& r) {
    return r.issues.empty() ? "" : r.issues.front().code;
}

}  // namespace

TEST_CASE("a design survives saving and opening in memory", "[io]") {
    auto doc = makeDesign();
    io::Bytes bytes;
    REQUIRE(io::writeBytes(*doc, bytes).empty());

    auto opened = io::readBytes(bytes, "tag.khan");
    REQUIRE(opened.ok());
    const auto& back = *opened.document;

    CHECK(back.id() == doc->id());
    CHECK(back.name() == "Keychain tag");
    CHECK(back.kind() == model::DocKind::Part);
    REQUIRE(back.featureCount() == doc->featureCount());
    for (std::size_t i = 0; i < doc->featureCount(); ++i) {
        CHECK(back.featureAt(i).id() == doc->featureAt(i).id());
        CHECK(back.featureAt(i).name() == doc->featureAt(i).name());
        CHECK(back.featureAt(i).type() == doc->featureAt(i).type());
        CHECK(back.featureAt(i).suppressed() == doc->featureAt(i).suppressed());
    }

    const auto& hole = static_cast<const model::CylinderFeature&>(back.featureAt(1));
    CHECK(hole.base().x == 9);
    CHECK(hole.base().z == -1);
    CHECK(hole.radius() == 2.5);
    CHECK(hole.height() == 6);
    const auto& tag = static_cast<const model::BooleanFeature&>(back.featureAt(2));
    CHECK(tag.op() == kernel::BoolOp::Cut);
    CHECK(tag.a() == doc->featureAt(0).id());
    CHECK(tag.b() == doc->featureAt(1).id());

    // Property: the rebuilt geometry is the same.
    test::FakeKernel kernel;
    const auto before = doc->regenerate(kernel);
    const auto after = back.regenerate(kernel);
    CHECK(after.ok());
    CHECK(after.bodies() == before.bodies());
    CHECK_THAT(kernel.massProps(*after.shapeOf(tag.id())).volume,
               WithinAbs(kernel.massProps(*before.shapeOf(tag.id())).volume, 1e-9));
    CHECK(after.result(back.featureAt(3).id())->status == model::FeatureStatus::Suppressed);
}

TEST_CASE("materials survive saving and opening", "[io][material]") {
    auto doc = makeDesign();
    const auto plate = doc->featureAt(0).id();
    const auto hole = doc->featureAt(1).id();
    model::Material blue{"", "Blue PETG", 1.27, {0x33, 0x66, 0xcc}, 2.1, 0.38, 50, 25, false};
    REQUIRE(doc->execute(std::make_unique<model::DefineMaterialCommand>(blue)).empty());
    REQUIRE(doc->execute(std::make_unique<model::SetMaterialCommand>(plate, "Blue PETG")).empty());
    REQUIRE(doc->execute(std::make_unique<model::SetMaterialCommand>(hole, "Steel (mild, 1018)")).empty());

    io::Bytes bytes;
    REQUIRE(io::writeBytes(*doc, bytes).empty());
    auto opened = io::readBytes(bytes, "tag.khan");
    REQUIRE(opened.ok());
    const auto& back = *opened.document;
    REQUIRE(back.customMaterials().size() == 1);
    CHECK(back.customMaterials()[0] == doc->customMaterials()[0]);  // same id, numbers and color
    CHECK(back.find(plate)->material() == doc->customMaterials()[0].key);
    CHECK(back.find(hole)->material() == "steel_1018");
    CHECK(back.materialOf(back.featureAt(2).id()).name == "Blue PETG");
    CHECK_FALSE(back.canUndo());  // loading materials isn't an undoable step

    // Saving it again gives the same bytes.
    io::Bytes again;
    REQUIRE(io::writeBytes(back, again).empty());
    CHECK(again == bytes);
}

TEST_CASE("a design without materials saves exactly as before", "[io][material]") {
    auto doc = makeDesign();
    io::Bytes bytes;
    REQUIRE(io::writeBytes(*doc, bytes).empty());
    const auto entries = readZip(bytes);
    REQUIRE(entries.size() == 2 + doc->featureCount());  // manifest, timeline, one per step
    CHECK_FALSE(entries.count("materials.json"));
    for (const auto& [name, text] : entries) CHECK(text.find("material") == std::string::npos);
}

TEST_CASE("a material key Khan doesn't know opens as the default material", "[io][material]") {
    const std::string box =
        R"({"id":")" + kBoxId +
        R"(","type":"Box","name":"Box1","material":"vibranium","params":{"corner":[0,0,0],"size":[10,20,30]}})";
    auto opened = io::readBytes(makeZip({{"manifest.json", manifest()},
                                         {"timeline.json", timeline()},
                                         {"features/" + kBoxId + ".json", box}}),
                                "odd.khan");
    REQUIRE(opened.ok());
    const auto& f = opened.document->featureAt(0);
    CHECK(f.material() == "vibranium");  // kept, so saving doesn't lose it
    CHECK(&opened.document->materialOf(f.id()) == &model::defaultMaterial());
}

TEST_CASE("a damaged materials file is refused with a plain message", "[io][material]") {
    const std::string withMaterials =
        R"({"format":"khan","schema":1,"app":"0.0.1","units":{"length":"mm","angle":"rad"},)"
        R"("document":{"id":")" +
        kDocId + R"(","kind":"part","name":"Hand made"},"files":["materials.json"]})";
    auto opened = io::readBytes(makeZip({{"manifest.json", withMaterials},
                                         {"timeline.json", timeline()},
                                         {"features/" + kBoxId + ".json", boxFeature()},
                                         {"materials.json", R"({"materials":[{"name":"No id"}]})"}}),
                                "bad.khan");
    CHECK_FALSE(opened.ok());
    CHECK(firstCode(opened) == "file.damaged");
}

TEST_CASE("parameters survive saving and opening", "[io][parameters]") {
    auto doc = makeDesign();
    REQUIRE(doc->execute(std::make_unique<model::SetParameterCommand>("wall", "3 mm", "How thick")).empty());
    REQUIRE(doc->execute(std::make_unique<model::SetParameterCommand>("height", "wall * 4")).empty());

    io::Bytes bytes;
    REQUIRE(io::writeBytes(*doc, bytes).empty());
    CHECK(readZip(bytes).count("parameters.json"));
    auto opened = io::readBytes(bytes, "tag.khan");
    REQUIRE(opened.ok());
    const auto& back = *opened.document;
    REQUIRE(back.parameters().parameters().size() == 2);
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& a = doc->parameters().parameters()[i];
        const auto& b = back.parameters().parameters()[i];
        CHECK(a.id == b.id);
        CHECK(a.name == b.name);
        CHECK(a.expression == b.expression);
        CHECK(a.note == b.note);
    }
    CHECK(back.parameters().evaluate().find("height")->value->value == 12);
    CHECK_FALSE(back.canUndo());  // loading parameters isn't an undoable step

    io::Bytes again;
    REQUIRE(io::writeBytes(back, again).empty());
    CHECK(again == bytes);
}

TEST_CASE("a dimension's formula survives saving and opening", "[io][parameters]") {
    model::Document doc;
    REQUIRE(doc.execute(std::make_unique<model::SetParameterCommand>("wall", "3 mm")).empty());
    sketch::Sketch s;
    const auto line = EntityId::generate();
    s.entities.push_back({line, "Line1", sketch::LineShape{{0, 0}, {10, 0}}, false});
    s.constraints.push_back({ConstraintId::generate(),
                             "Length1",
                             sketch::ConstraintKind::Length,
                             {},
                             {line},
                             12,
                             {},
                             "wall * 4"});
    s.constraints.push_back(
        {ConstraintId::generate(), "Length2", sketch::ConstraintKind::Length, {}, {line}, 12, {}, {}});
    REQUIRE(doc.execute(std::make_unique<model::AddFeatureCommand>(std::make_unique<model::SketchFeature>(
                            FeatureId::generate(), "Sketch1", model::SketchPlane::XY, s)))
                .empty());

    io::Bytes bytes;
    REQUIRE(io::writeBytes(doc, bytes).empty());
    const auto entries = readZip(bytes);
    const auto& stepText = entries.at("features/" + doc.featureAt(0).id().toString() + ".json");
    CHECK(stepText.find("\"expression\": \"wall * 4\"") != std::string::npos);
    CHECK(stepText.find("expression") == stepText.rfind("expression"));  // only the one with a formula
    auto opened = io::readBytes(bytes, "s.khan");
    REQUIRE(opened.ok());
    const auto& back = static_cast<const model::SketchFeature&>(opened.document->featureAt(0)).sketch();
    CHECK(back.findConstraint("Length1")->expression == "wall * 4");
    CHECK(back.findConstraint("Length1")->value == 12);
    CHECK(back.findConstraint("Length2")->expression.empty());
}

TEST_CASE("a step's formulas survive saving and opening, and copying", "[io][parameters]") {
    auto doc = makeDesign();
    REQUIRE(doc->execute(std::make_unique<model::SetParameterCommand>("wall", "3 mm")).empty());
    const auto plate = doc->featureAt(0).id();
    REQUIRE(doc->execute(std::make_unique<model::SetNumberCommand>(plate, "size_z", std::string("wall * 2")))
                .empty());
    auto copy = doc->find(plate)->clone();
    copy->setFormula("glow", "wall");  // a key from a newer Khan is kept
    REQUIRE(doc->execute(std::make_unique<model::EditFeatureCommand>(std::move(copy))).empty());

    io::Bytes bytes;
    REQUIRE(io::writeBytes(*doc, bytes).empty());
    const auto entries = readZip(bytes);
    CHECK(entries.at("features/" + plate.toString() + ".json").find("\"formulas\"") != std::string::npos);
    CHECK(entries.at("features/" + doc->featureAt(1).id().toString() + ".json").find("formulas") ==
          std::string::npos);
    auto opened = io::readBytes(bytes, "tag.khan");
    REQUIRE(opened.ok());
    const auto& back = *opened.document->find(plate);
    CHECK(back.formulas() == doc->find(plate)->formulas());
    CHECK(back.number("size_z") == 6);

    const auto text = io::stepsToText(*doc, {plate});
    auto pasted = io::stepsFromText(text);
    REQUIRE(pasted.ok());
    CHECK(pasted.steps.front()->formula("size_z") == "wall * 2");
}

TEST_CASE("a design without parameters has no parameters file", "[io][parameters]") {
    auto doc = makeDesign();
    io::Bytes bytes;
    REQUIRE(io::writeBytes(*doc, bytes).empty());
    CHECK_FALSE(readZip(bytes).count("parameters.json"));
}

TEST_CASE("a parameter with a problem still opens, so it can be fixed", "[io][parameters]") {
    const std::string list = R"({"parameters":[{"id":"6f1c2a52-3d4e-4f60-8a7b-9c0d1e2f3a4b","name":"height",)"
                             R"j("expression":"wall * sideways(4)"}]})j";
    auto opened = io::readBytes(makeZip({{"manifest.json", manifest()},
                                         {"timeline.json", timeline()},
                                         {"features/" + kBoxId + ".json", boxFeature()},
                                         {"parameters.json", list}}),
                                "odd.khan");
    REQUIRE(opened.ok());
    const auto& table = opened.document->parameters();
    REQUIRE(table.find("height"));
    CHECK(table.find("height")->expression == "wall * sideways(4)");
    CHECK(table.evaluate().find("height")->issues.front().code == "expression.function");
}

TEST_CASE("a damaged parameters file is refused with a plain message", "[io][parameters]") {
    for (const std::string list :
         {R"({"parameters":{}})", R"({"parameters":[{"name":"wall","expression":"3"}]})",
          R"({"parameters":[{"id":"6f1c2a52-3d4e-4f60-8a7b-9c0d1e2f3a4b","name":"mm",)"
          R"("expression":"3"}]})",
          "not json"}) {
        INFO(list);
        auto opened = io::readBytes(makeZip({{"manifest.json", manifest()},
                                             {"timeline.json", timeline()},
                                             {"features/" + kBoxId + ".json", boxFeature()},
                                             {"parameters.json", list}}),
                                    "bad.khan");
        CHECK_FALSE(opened.ok());
        CHECK(firstCode(opened) == "file.damaged");
    }
}

TEST_CASE("an opened design has no undo history and no unsaved changes", "[io]") {
    auto doc = makeDesign();
    io::Bytes bytes;
    REQUIRE(io::writeBytes(*doc, bytes).empty());
    auto opened = io::readBytes(bytes, "tag.khan");
    REQUIRE(opened.ok());
    CHECK_FALSE(opened.document->canUndo());
    CHECK_FALSE(opened.document->canRedo());
    CHECK_FALSE(opened.document->isModified());
}

TEST_CASE("saving the same design twice gives the same bytes", "[io]") {
    auto doc = makeDesign();
    io::Bytes first, second;
    REQUIRE(io::writeBytes(*doc, first).empty());
    REQUIRE(io::writeBytes(*doc, second).empty());
    CHECK(first == second);

    // And a design that was opened and saved again is unchanged too.
    auto opened = io::readBytes(first, "tag.khan");
    REQUIRE(opened.ok());
    io::Bytes third;
    REQUIRE(io::writeBytes(*opened.document, third).empty());
    CHECK(third == first);
}

#ifndef _WIN32
#include <cstdlib>
#include <ctime>

// Bug found while building this: libzip's zip_file_set_mtime goes through the local
// time zone, so the same design gave different bytes in Chicago and in London.
TEST_CASE("the bytes don't depend on the computer's time zone", "[io]") {
    auto doc = makeDesign();
    const char* old = std::getenv("TZ");
    const std::string saved = old ? old : "";
    io::Bytes utc, chicago;
    setenv("TZ", "UTC", 1);
    tzset();
    REQUIRE(io::writeBytes(*doc, utc).empty());
    setenv("TZ", "America/Chicago", 1);
    tzset();
    REQUIRE(io::writeBytes(*doc, chicago).empty());
    if (old)
        setenv("TZ", saved.c_str(), 1);
    else
        unsetenv("TZ");
    tzset();
    CHECK(utc == chicago);
}
#endif

TEST_CASE("saveFile writes a file that openFile reads, and marks the design saved", "[io]") {
    const auto dir = std::filesystem::temp_directory_path() / "kh_io_test";
    std::filesystem::create_directories(dir);
    const auto path = dir / "tag.khan";

    auto doc = makeDesign();
    REQUIRE(doc->isModified());
    REQUIRE(io::saveFile(*doc, path).empty());
    CHECK_FALSE(doc->isModified());
    CHECK_FALSE(std::filesystem::exists(dir / "tag.khan.saving"));

    // Saving over an existing file replaces it.
    REQUIRE(io::saveFile(*doc, path).empty());

    auto opened = io::openFile(path);
    REQUIRE(opened.ok());
    CHECK(opened.document->featureCount() == 4);
    std::filesystem::remove_all(dir);
}

TEST_CASE("saving into a folder that doesn't exist explains the problem", "[io]") {
    auto doc = makeDesign();
    const auto path = std::filesystem::temp_directory_path() / "kh_no_such_folder" / "x" / "tag.khan";
    const auto issues = io::saveFile(*doc, path);
    REQUIRE(issues.size() == 1);
    CHECK(issues.front().code == "file.cant_write");
    CHECK(issues.front().message.find("tag.khan") != std::string::npos);
    CHECK(doc->isModified());  // not saved
}

TEST_CASE("opening a file that isn't there explains the problem", "[io]") {
    auto r = io::openFile(std::filesystem::temp_directory_path() / "kh_missing_design.khan");
    CHECK_FALSE(r.ok());
    CHECK(firstCode(r) == "file.cant_read");
}

TEST_CASE("files that aren't Khan designs are refused", "[io]") {
    SECTION("not a zip") {
        const std::string text = "solid cube\nendsolid cube\n";
        auto r = io::readBytes(io::Bytes(text.begin(), text.end()), "cube.stl");
        CHECK_FALSE(r.ok());
        CHECK(firstCode(r) == "file.not_khan");
        CHECK(r.issues.front().message.find("cube.stl") != std::string::npos);
    }
    SECTION("a zip without a manifest") {
        auto r = io::readBytes(makeZip({{"readme.txt", "hello"}}), "photos.zip");
        CHECK(firstCode(r) == "file.not_khan");
    }
    SECTION("a manifest for some other format") {
        auto r = io::readBytes(makeZip({{"manifest.json", R"({"format":"other","schema":1})"}}), "x.khan");
        CHECK(firstCode(r) == "file.not_khan");
    }
}

TEST_CASE("damaged files are refused with a plain message", "[io]") {
    SECTION("manifest is not JSON") {
        auto r = io::readBytes(makeZip({{"manifest.json", "{not json"}}), "x.khan");
        CHECK(firstCode(r) == "file.damaged");
    }
    SECTION("timeline missing") {
        auto r = io::readBytes(makeZip({{"manifest.json", manifest()}}), "x.khan");
        CHECK(firstCode(r) == "file.damaged");
    }
    SECTION("feature file missing") {
        auto r =
            io::readBytes(makeZip({{"manifest.json", manifest()}, {"timeline.json", timeline()}}), "x.khan");
        CHECK(firstCode(r) == "file.damaged");
    }
    SECTION("a number is missing") {
        const std::string bad =
            R"({"id":")" + kBoxId + R"(","type":"Box","name":"Box1","params":{"corner":[0,0]}})";
        auto r = io::readBytes(makeZip({{"manifest.json", manifest()},
                                        {"timeline.json", timeline()},
                                        {"features/" + kBoxId + ".json", bad}}),
                               "x.khan");
        CHECK(firstCode(r) == "file.damaged");
        CHECK(r.issues.front().detail.find("corner") != std::string::npos);
    }
    SECTION("a step uses a step that isn't in the file") {
        const std::string missing = "11111111-2222-4333-8444-555555555555";
        const std::string cut = R"({"id":")" + kBoxId +
                                R"(","type":"Boolean","name":"Cut1",)"
                                R"("inputs":{"a":")" +
                                missing + R"(","b":")" + missing + R"("},"params":{"operation":"cut"}})";
        auto r = io::readBytes(makeZip({{"manifest.json", manifest()},
                                        {"timeline.json", timeline()},
                                        {"features/" + kBoxId + ".json", cut}}),
                               "x.khan");
        CHECK_FALSE(r.ok());
        CHECK(firstCode(r) == "file.damaged");
    }
}

TEST_CASE("files from a newer Khan ask the student to update", "[io]") {
    auto r = io::readBytes(makeZip({{"manifest.json", manifest(kFileSchemaVersion + 1)}}), "future.khan");
    CHECK_FALSE(r.ok());
    CHECK(firstCode(r) == "file.too_new");
    CHECK(r.issues.front().message.find("Update Khan") != std::string::npos);
}

TEST_CASE("an unknown kind of step is named in the message", "[io]") {
    auto r = io::readBytes(makeZip({{"manifest.json", manifest()},
                                    {"timeline.json", timeline()},
                                    {"features/" + kBoxId + ".json", boxFeature("Loft")}}),
                           "x.khan");
    CHECK_FALSE(r.ok());
    CHECK(firstCode(r) == "file.unknown_step");
    CHECK(r.issues.front().message.find("Box1") != std::string::npos);
    CHECK(r.issues.front().message.find("Loft") != std::string::npos);
}

TEST_CASE("a hand-made file with only the required parts opens", "[io]") {
    auto r = io::readBytes(makeZip({{"manifest.json", manifest()},
                                    {"timeline.json", timeline()},
                                    {"features/" + kBoxId + ".json", boxFeature()}}),
                           "x.khan");
    REQUIRE(r.ok());
    CHECK(r.document->id().toString() == kDocId);
    REQUIRE(r.document->featureCount() == 1);
    const auto& box = static_cast<const model::BoxFeature&>(r.document->featureAt(0));
    CHECK(box.id().toString() == kBoxId);
    CHECK(box.size().y == 20);
}

// A schema-1 file checked into the repo. Every future version of Khan must still
// open it (through a migration if the schema changes). Never regenerate it.
TEST_CASE("the checked-in schema 1 file still opens", "[io][compat]") {
    auto r = io::openFile(std::filesystem::path(KH_TEST_FILES_DIR) / "schema1-keychain-tag.khan");
    REQUIRE(r.ok());
    const auto& doc = *r.document;
    CHECK(doc.id().toString() == "a3c4e5f6-0718-4293-a4b5-c6d7e8f90a1b");
    CHECK(doc.name() == "Keychain tag");
    REQUIRE(doc.featureCount() == 3);
    CHECK(doc.featureAt(0).name() == "Plate");
    CHECK(doc.featureAt(1).name() == "Hole");
    CHECK(doc.featureAt(2).name() == "Tag");

    test::FakeKernel kernel;
    const auto report = doc.regenerate(kernel);
    REQUIRE(report.ok());
    // Fake volumes: plate 60*30*4 = 7200, hole 3*2.5^2*6 = 112.5 (FakeKernel uses 3 for pi).
    CHECK_THAT(kernel.massProps(*report.shapeOf(doc.featureAt(2).id())).volume, WithinAbs(7087.5, 1e-9));
}

// ---------------------------------------------------------------------------
// Steps as text (copy and paste)
// ---------------------------------------------------------------------------

TEST_CASE("copied steps read back with new ids, the same numbers and the steps they use", "[io][copy]") {
    auto doc = makeDesign();
    const auto tag = doc->featureAt(2).id();
    const std::string text = io::stepsToText(*doc, {tag});
    CHECK(io::looksLikeSteps(text));

    auto pasted = io::stepsFromText(text);
    REQUIRE(pasted.ok());
    REQUIRE(pasted.steps.size() == 3);
    CHECK(pasted.steps[0]->name() == "Plate");
    CHECK(pasted.steps[2]->name() == "Tag");
    for (const auto& f : pasted.steps) CHECK_FALSE(doc->find(f->id()));
    CHECK(pasted.steps[2]->dependencies() ==
          std::vector<FeatureId>{pasted.steps[0]->id(), pasted.steps[1]->id()});
    const auto& hole = dynamic_cast<const model::CylinderFeature&>(*pasted.steps[1]);
    CHECK_THAT(hole.radius(), WithinAbs(2.5, 1e-12));

    // Into another design: it rebuilds to the same tag.
    model::Document other;
    REQUIRE(other
                .execute(std::make_unique<model::AddFeaturesCommand>(std::move(pasted.steps),
                                                                     std::move(pasted.materials), "Paste"))
                .empty());
    test::FakeKernel k;
    const auto report = other.regenerate(k);
    REQUIRE(report.ok());
    CHECK(test::FakeKernel::volumeOf(*report.shapeOf(other.featureAt(2).id())) ==
          test::FakeKernel::volumeOf(*doc->regenerate(k).shapeOf(tag)));
}

TEST_CASE("copied steps keep being turned off, and bring their own materials", "[io][copy][material]") {
    auto doc = makeDesign();
    const auto plate = doc->featureAt(0).id();
    model::Material blue{"", "Blue PETG", 1.27, {0x33, 0x66, 0xcc}, 2.1, 0.38, 50, 25, false};
    REQUIRE(doc->execute(std::make_unique<model::DefineMaterialCommand>(blue)).empty());
    REQUIRE(doc->execute(std::make_unique<model::SetMaterialCommand>(plate, "Blue PETG")).empty());

    auto pasted = io::stepsFromText(io::stepsToText(*doc, {plate, doc->featureAt(3).id()}));
    REQUIRE(pasted.steps.size() == 2);
    CHECK(pasted.steps[0]->material() == doc->customMaterials()[0].key);
    CHECK(pasted.steps[1]->suppressed());  // Spare was turned off
    REQUIRE(pasted.materials.size() == 1);
    CHECK(pasted.materials[0] == doc->customMaterials()[0]);
}

TEST_CASE("pasting text that isn't steps explains why", "[io][copy]") {
    auto code = [](std::string_view text) {
        auto r = io::stepsFromText(text);
        CHECK_FALSE(r.ok());
        REQUIRE(r.issues.size() == 1);
        CHECK_FALSE(r.issues[0].message.empty());
        return r.issues[0].code;
    };
    CHECK(code("hello") == "paste.not_steps");
    CHECK(code(R"({"format":"khan-steps","version":1,"steps":[]})") == "paste.not_steps");
    CHECK(code(R"({"format":"khan-steps","version":2,"steps":[]})") == "paste.too_new");
    CHECK(code(R"({"format":"khan-steps","version":1,"steps":[{"name":"Box1"}]})") == "paste.damaged");
    CHECK_FALSE(io::looksLikeSteps("hello"));
    CHECK_FALSE(io::looksLikeSteps(""));

    // A step that uses a step that wasn't copied.
    auto doc = makeDesign();
    const std::string text = io::stepsToText(*doc, {doc->featureAt(2).id()});
    // Point Tag at a step that isn't in the copy: change the second mention of Plate's id.
    auto j = text;
    const auto idAt = j.find("\"id\"");
    REQUIRE(idAt != std::string::npos);
    const auto start = j.find('"', j.find(':', idAt)) + 1;
    const std::string plateId = j.substr(start, j.find('"', start) - start);
    const auto again = j.find(plateId, start + plateId.size());
    REQUIRE(again != std::string::npos);
    j.replace(again, plateId.size(), FeatureId::generate().toString());
    CHECK(code(j) == "paste.damaged");
}

TEST_CASE("copying a step that isn't in the design gives no text", "[io][copy]") {
    auto doc = makeDesign();
    CHECK(io::stepsToText(*doc, {FeatureId::generate()}).empty());
}

// ---------------------------------------------------------------------------
// Folders
// ---------------------------------------------------------------------------

TEST_CASE("folders survive saving and opening", "[io][folders]") {
    auto doc = makeDesign();
    const auto plate = doc->featureAt(0).id();
    const auto hole = doc->featureAt(1).id();
    REQUIRE(
        doc->execute(std::make_unique<model::AddFolderCommand>(std::vector<FeatureId>{plate, hole}, "Parts"))
            .empty());
    io::Bytes bytes;
    REQUIRE(io::writeBytes(*doc, bytes).empty());
    auto opened = io::readBytes(bytes, "tag.khan");
    REQUIRE(opened.ok());
    CHECK(opened.document->folders() == doc->folders());  // same id, name and steps
    CHECK_FALSE(opened.document->canUndo());              // loading folders isn't an undoable step

    io::Bytes again;
    REQUIRE(io::writeBytes(*opened.document, again).empty());
    CHECK(again == bytes);
}

TEST_CASE("a design without folders saves exactly as before", "[io][folders]") {
    auto doc = makeDesign();
    io::Bytes before;
    REQUIRE(io::writeBytes(*doc, before).empty());
    REQUIRE(doc->execute(std::make_unique<model::AddFolderCommand>(
                             std::vector<FeatureId>{doc->featureAt(0).id()}, "Parts"))
                .empty());
    REQUIRE(doc->undo());
    io::Bytes after;
    REQUIRE(io::writeBytes(*doc, after).empty());
    CHECK(after == before);
}

TEST_CASE("a damaged folders file is refused with a plain message", "[io][folders]") {
    auto doc = makeDesign();
    io::Bytes good;
    REQUIRE(io::writeBytes(*doc, good).empty());
    auto withFolders = [&](const std::string& text) {
        // Copy every entry of the good file, and add folders.json.
        std::map<std::string, std::string> entries;
        zip_error_t err;
        zip_error_init(&err);
        zip_source_t* src = zip_source_buffer_create(good.data(), good.size(), 0, &err);
        zip_t* za = zip_open_from_source(src, ZIP_RDONLY, &err);
        for (zip_int64_t i = 0; i < zip_get_num_entries(za, 0); ++i) {
            const char* name = zip_get_name(za, static_cast<zip_uint64_t>(i), 0);
            zip_stat_t st;
            zip_stat_index(za, static_cast<zip_uint64_t>(i), 0, &st);
            std::string data(st.size, '\0');
            zip_file_t* zf = zip_fopen_index(za, static_cast<zip_uint64_t>(i), 0);
            zip_fread(zf, data.data(), st.size);
            zip_fclose(zf);
            entries[name] = data;
        }
        zip_close(za);
        entries["folders.json"] = text;
        return io::readBytes(makeZip(entries), "tag.khan");
    };
    const std::string plate = doc->featureAt(0).id().toString();
    const std::string tag = doc->featureAt(2).id().toString();
    const std::string folderId = FolderId::generate().toString();

    auto ok = withFolders(R"({"folders":[{"id":")" + folderId + R"(","name":"Base","steps":[")" + plate +
                          R"("]}]})");
    REQUIRE(ok.ok());
    REQUIRE(ok.document->folders().size() == 1);
    CHECK(ok.document->folders()[0].name == "Base");

    for (const std::string& bad :
         {std::string(R"({"folders":3})"), std::string(R"({"folders":[{"name":"Base","steps":[]}]})"),
          R"({"folders":[{"id":")" + folderId + R"(","name":"Base","steps":["nope"]}]})",
          // Plate and Tag aren't next to each other.
          R"({"folders":[{"id":")" + folderId + R"(","name":"Base","steps":[")" + plate + R"(",")" + tag +
              R"("]}]})",
          std::string("not json")}) {
        auto r = withFolders(bad);
        CHECK_FALSE(r.ok());
        REQUIRE(r.issues.size() == 1);
        CHECK(r.issues[0].code == "file.damaged");
    }
}

// ---------------------------------------------------------------------------
// Move steps
// ---------------------------------------------------------------------------

TEST_CASE("a Move step survives saving and opening, with its turn in radians", "[io][move]") {
    auto doc = makeDesign();
    const auto tag = doc->featureAt(2).id();
    kernel::Motion m{{10, 20, 5}, {30, 15, 0}, {0, 0, 1}, 0.5};
    auto move = std::make_unique<model::MoveFeature>("Move1", tag, m);
    const auto id = move->id();
    REQUIRE(doc->execute(std::make_unique<model::AddFeatureCommand>(std::move(move))).empty());

    io::Bytes bytes;
    REQUIRE(io::writeBytes(*doc, bytes).empty());
    auto opened = io::readBytes(bytes, "tag.khan");
    REQUIRE(opened.ok());
    const auto* back = dynamic_cast<const model::MoveFeature*>(opened.document->find(id));
    REQUIRE(back);
    CHECK(back->body() == tag);
    CHECK(back->motion().offset.y == 20.0);
    CHECK(back->motion().center.x == 30.0);
    CHECK(back->motion().axis.z == 1.0);
    CHECK(back->motion().angle == 0.5);

    io::Bytes again;
    REQUIRE(io::writeBytes(*opened.document, again).empty());
    CHECK(again == bytes);

    // Copy and paste keep it too.
    auto pasted = io::stepsFromText(io::stepsToText(*doc, {id}));
    REQUIRE(pasted.steps.size() == 4);  // Plate, Hole, Tag, Move1
    const auto& copy = dynamic_cast<const model::MoveFeature&>(*pasted.steps[3]);
    CHECK(copy.body() == pasted.steps[2]->id());
    CHECK(copy.motion().angle == 0.5);
}

// ---------------------------------------------------------------------------
// Sketch steps
// ---------------------------------------------------------------------------

namespace {

// A sketch with every kind of entity and constraint, and a construction line.
sketch::Sketch everyKind() {
    using namespace kh::sketch;
    Sketch s;
    auto add = [&](std::string name, Shape shape, bool construction = false) {
        s.entities.push_back({EntityId::generate(), std::move(name), shape, construction});
        return s.entities.back().id;
    };
    const auto p = add("Point1", PointShape{{1, 2}});
    const auto l1 = add("Line1", LineShape{{0, 0}, {40, 0}});
    const auto l2 = add("Line2", LineShape{{0, 0}, {0, 20}}, true);
    const auto c = add("Circle1", CircleShape{{20, 10}, 5});
    const auto a = add("Arc1", ArcShape{{40, 10}, 10, -1.5707963, 1.5707963});
    const auto sp =
        add("Spline1", SplineShape{{{0, 30}, {10, 40}, {30, 40}, {40, 30}}, {1, 2.5, 1, 1}, 3, false});
    // Imported from a file (as control points, in inches): the file is remembered, letters and all.
    SplineShape imported{{{0, 50}, {10, 50}, {10, 60}, {0, 60}}, {}, 2, true};
    imported.source = SplineSource{"C:/Users/Zoë/wave points.csv", false, LengthUnit::Inch};
    add("Spline2", imported);
    auto con = [&](std::string name, ConstraintKind k, std::vector<PointRef> pts, std::vector<EntityId> ents,
                   double v = 0, Point2 at = {}) {
        s.constraints.push_back(
            {ConstraintId::generate(), std::move(name), k, std::move(pts), std::move(ents), v, at});
    };
    using K = ConstraintKind;
    con("Fix1", K::Fix, {{l1, PointRole::Start}}, {}, 0, {0, 0});
    con("Coincident1", K::Coincident, {{l1, PointRole::Start}, {l2, PointRole::Start}}, {});
    con("Horizontal1", K::Horizontal, {}, {l1});
    con("Vertical1", K::Vertical, {}, {l2});
    con("Parallel1", K::Parallel, {}, {l1, l2});
    con("Perpendicular1", K::Perpendicular, {}, {l1, l2});
    con("Equal1", K::Equal, {}, {c, a});
    con("PointOnLine1", K::PointOnLine, {{p, PointRole::Self}}, {l1});
    con("PointOnCircle1", K::PointOnCircle, {{p, PointRole::Self}}, {c});
    con("Midpoint1", K::Midpoint, {{p, PointRole::Self}}, {l1});
    con("Symmetric1", K::Symmetric, {{p, PointRole::Self}, {c, PointRole::Center}}, {l2});
    con("Tangent1", K::Tangent, {}, {l1, a});
    con("Distance1", K::Distance, {{p, PointRole::Self}, {c, PointRole::Center}}, {}, 12.5);
    con("Length1", K::Length, {}, {l1}, 40);
    con("HorizontalDistance1", K::HorizontalDistance, {{l1, PointRole::Start}, {a, PointRole::Center}}, {},
        -3);
    con("VerticalDistance1", K::VerticalDistance, {{l1, PointRole::Start}, {a, PointRole::End}}, {}, 20);
    con("Radius1", K::Radius, {}, {c}, 5);
    con("Diameter1", K::Diameter, {}, {a}, 20);
    con("Angle1", K::Angle, {}, {l1, l2}, 1.5707963);
    con("Coincident2", K::Coincident, {{sp, PointRole::Pole, 2}, {sp, PointRole::Start}}, {});
    return s;
}

bool sameSketch(const sketch::Sketch& a, const sketch::Sketch& b) {
    if (a.entities.size() != b.entities.size() || a.constraints.size() != b.constraints.size()) return false;
    for (std::size_t i = 0; i < a.entities.size(); ++i) {
        const auto& x = a.entities[i];
        const auto& y = b.entities[i];
        if (x.id != y.id || x.name != y.name || x.construction != y.construction ||
            x.shape.index() != y.shape.index())
            return false;
        if (const auto* s = std::get_if<sketch::SplineShape>(&x.shape)) {
            const auto& t = std::get<sketch::SplineShape>(y.shape);
            if (s->weights != t.weights || s->degree != t.degree || s->closed != t.closed ||
                s->poles.size() != t.poles.size() || s->source != t.source)
                return false;
            for (std::size_t k = 0; k < s->poles.size(); ++k) {
                if (s->poles[k].x != t.poles[k].x || s->poles[k].y != t.poles[k].y) return false;
            }
        }
    }
    for (std::size_t i = 0; i < a.constraints.size(); ++i) {
        const auto& x = a.constraints[i];
        const auto& y = b.constraints[i];
        if (x.id != y.id || x.name != y.name || x.kind != y.kind || x.value != y.value || x.at.x != y.at.x ||
            x.entities != y.entities || x.points.size() != y.points.size())
            return false;
        for (std::size_t j = 0; j < x.points.size(); ++j) {
            if (!(x.points[j] == y.points[j])) return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("an Extrude step survives saving and opening, and pastes with its sketch", "[io][extrude]") {
    model::Document doc(model::DocKind::Part, "Plate");
    auto sketchStep = std::make_unique<model::SketchFeature>(FeatureId::generate(), "Sketch1",
                                                             model::SketchPlane::XZ, everyKind());
    const auto sketchId = sketchStep->id();
    REQUIRE(doc.execute(std::make_unique<model::AddFeatureCommand>(std::move(sketchStep))).empty());
    auto extrude = std::make_unique<model::ExtrudeFeature>("Plate", sketchId, 4.5, model::ExtrudeSide::Both);
    const auto id = extrude->id();
    REQUIRE(doc.execute(std::make_unique<model::AddFeatureCommand>(std::move(extrude))).empty());

    io::Bytes bytes;
    REQUIRE(io::writeBytes(doc, bytes).empty());
    auto opened = io::readBytes(bytes, "plate.khan");
    REQUIRE(opened.ok());
    const auto* back = dynamic_cast<const model::ExtrudeFeature*>(opened.document->find(id));
    REQUIRE(back);
    CHECK(back->sketch() == sketchId);
    CHECK(back->distance() == 4.5);
    CHECK(back->side() == model::ExtrudeSide::Both);
    io::Bytes again;
    REQUIRE(io::writeBytes(*opened.document, again).empty());
    CHECK(again == bytes);

    // Pasted, it comes with its sketch, and uses the pasted sketch.
    auto pasted = io::stepsFromText(io::stepsToText(doc, {id}));
    REQUIRE(pasted.steps.size() == 2);
    const auto& copy = dynamic_cast<const model::ExtrudeFeature&>(*pasted.steps[1]);
    CHECK(copy.sketch() == pasted.steps[0]->id());
    CHECK(copy.side() == model::ExtrudeSide::Both);
}

TEST_CASE("an Extrude with a side Khan doesn't know is refused, naming the step", "[io][extrude]") {
    const std::string sketchId = "1a2b3c4d-1111-4222-8333-444455556666";
    const std::string extrudeId = "5e6f7a8b-1111-4222-8333-444455556666";
    const auto bytes = makeZip(
        {{"manifest.json", manifest()},
         {"timeline.json", R"({"features":[{"id":")" + sketchId + R"(","suppressed":false},{"id":")" +
                               extrudeId + R"(","suppressed":false}]})"},
         {"features/" + sketchId + ".json",
          R"({"id":")" + sketchId +
              R"(","name":"Sketch1","type":"Sketch","params":{"plane":"xy","entities":[],"constraints":[]}})"},
         {"features/" + extrudeId + ".json",
          R"({"id":")" + extrudeId + R"(","name":"Plate","type":"Extrude","inputs":{"sketch":")" + sketchId +
              R"("},"params":{"distance":5,"side":"sideways"}})"}});
    const auto r = io::readBytes(bytes, "odd.khan");
    REQUIRE_FALSE(r.ok());
    CHECK(r.issues.at(0).detail.find("step Plate: bad or missing side") != std::string::npos);
}

TEST_CASE("a sketch with every kind of entity and constraint survives saving and opening", "[io][sketch]") {
    REQUIRE(sketch::check(everyKind()).empty());
    model::Document doc(model::DocKind::Part, "Plate");
    auto step = std::make_unique<model::SketchFeature>(FeatureId::generate(), "Sketch1",
                                                       model::SketchPlane::YZ, everyKind());
    const auto id = step->id();
    REQUIRE(doc.execute(std::make_unique<model::AddFeatureCommand>(std::move(step))).empty());

    io::Bytes bytes;
    REQUIRE(io::writeBytes(doc, bytes).empty());
    auto opened = io::readBytes(bytes, "plate.khan");
    REQUIRE(opened.ok());
    const auto* back = dynamic_cast<const model::SketchFeature*>(opened.document->find(id));
    REQUIRE(back);
    CHECK(back->plane() == model::SketchPlane::YZ);
    CHECK(sameSketch(back->sketch(), static_cast<const model::SketchFeature&>(*doc.find(id)).sketch()));
    const auto& arc = std::get<sketch::ArcShape>(back->sketch().entities[4].shape);
    CHECK(arc.radius == 10);
    CHECK(arc.startAngle == -1.5707963);
    CHECK(back->sketch().entities[2].construction);

    io::Bytes again;
    REQUIRE(io::writeBytes(*opened.document, again).empty());
    CHECK(again == bytes);
}

TEST_CASE("a damaged sketch is refused with a plain message", "[io][sketch]") {
    // A constraint that points at an entity that isn't in the sketch.
    sketch::Sketch s = everyKind();
    s.constraints[2].entities[0] = EntityId::generate();
    model::Document doc(model::DocKind::Part, "Plate");
    REQUIRE(doc.execute(std::make_unique<model::AddFeatureCommand>(std::make_unique<model::SketchFeature>(
                            FeatureId::generate(), "Sketch1", model::SketchPlane::XY, s)))
                .empty());
    io::Bytes bytes;
    REQUIRE(io::writeBytes(doc, bytes).empty());
    auto opened = io::readBytes(bytes, "plate.khan");
    REQUIRE_FALSE(opened.ok());
    REQUIRE(opened.issues.size() == 1);
    CHECK(opened.issues[0].code == "file.damaged");
    CHECK(opened.issues[0].detail.find("Horizontal uses something that isn't in the sketch") !=
          std::string::npos);
}

TEST_CASE("the design's unit survives saving and opening; designs in mm save as before", "[io][units]") {
    auto doc = makeDesign();
    io::Bytes before;
    REQUIRE(io::writeBytes(*doc, before).empty());
    CHECK(readZip(before).at("manifest.json").find("lengthUnit") == std::string::npos);

    REQUIRE(doc->execute(std::make_unique<model::SetUnitsCommand>(LengthUnit::Inch)).empty());
    io::Bytes bytes;
    REQUIRE(io::writeBytes(*doc, bytes).empty());
    const auto manifestText = readZip(bytes).at("manifest.json");
    CHECK(manifestText.find(R"("lengthUnit": "in")") != std::string::npos);
    CHECK(manifestText.find(R"("length": "mm")") != std::string::npos);  // the numbers are still in mm
    auto opened = io::readBytes(bytes, "tag.khan");
    REQUIRE(opened.ok());
    CHECK(opened.document->lengthUnit() == LengthUnit::Inch);
    CHECK_FALSE(opened.document->canUndo());  // opening isn't an undoable step
    // Every length is unchanged.
    CHECK(static_cast<const model::BoxFeature&>(opened.document->featureAt(0)).size().x == 60);
}

TEST_CASE("a unit Khan doesn't know opens in mm", "[io][units]") {
    std::string withUnit = manifest();
    withUnit.replace(withUnit.find(R"("name":"Hand made")"), 18,
                     R"("name":"Hand made","lengthUnit":"cubit")");
    auto opened = io::readBytes(makeZip({{"manifest.json", withUnit},
                                         {"timeline.json", timeline()},
                                         {"features/" + kBoxId + ".json", boxFeature()}}),
                                "odd.khan");
    REQUIRE(opened.ok());
    CHECK(opened.document->lengthUnit() == LengthUnit::Millimeter);
}

TEST_CASE("a spline with too few poles, or a pole point without its number, is refused",
          "[io][sketch][spline]") {
    const std::string sketchId = "1a2b3c4d-1111-4222-8333-444455556666";
    auto open = [&](const std::string& entities, const std::string& constraints) {
        return io::readBytes(
            makeZip({{"manifest.json", manifest()},
                     {"timeline.json", R"({"features":[{"id":")" + sketchId + R"(","suppressed":false}]})"},
                     {"features/" + sketchId + ".json",
                      R"({"id":")" + sketchId +
                          R"(","name":"Sketch1","type":"Sketch","params":{"plane":"xy","entities":[)" +
                          entities + R"(],"constraints":[)" + constraints + "]}}"}}),
            "odd.khan");
    };
    const std::string spline =
        R"({"id":"2b2b3c4d-1111-4222-8333-444455556666","name":"Spline1","type":"spline","poles":[[0,0],[1,1],[2,0],[3,1]],"degree":3})";
    CHECK(open(spline, "").ok());
    const auto noDegree = open(
        R"({"id":"2b2b3c4d-1111-4222-8333-444455556666","name":"Spline1","type":"spline","poles":[[0,0],[1,1]]})",
        "");
    REQUIRE_FALSE(noDegree.ok());
    CHECK(noDegree.issues.at(0).detail.find("Spline1: bad or missing degree") != std::string::npos);
    const auto badIndex = open(
        spline,
        R"({"id":"3c2b3c4d-1111-4222-8333-444455556666","name":"Fix1","kind":"fix","at":[0,0],"points":[{"entity":"2b2b3c4d-1111-4222-8333-444455556666","role":"pole","index":"two"}]})");
    REQUIRE_FALSE(badIndex.ok());
    CHECK(badIndex.issues.at(0).detail.find("Fix1: bad point") != std::string::npos);
}
