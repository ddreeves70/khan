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
