#include "kh/io/KhanFile.h"
#include "kh/io/Steps.h"

#include <zip.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "kh/core/Version.h"
#include "kh/model/Commands.h"

// Format (schema 1), as described in docs/file-format.md:
//
//   manifest.json        format, schema, app version, units, document id/kind/name, file list
//   timeline.json        feature order and suppressed flags
//   features/<id>.json   one file per feature: id, type, name, inputs, params
//   materials.json       the design's own materials (only if it has any)
//   folders.json         the feature tree's folders (only if it has any)
//
// Lengths are millimeters. JSON is written with sorted keys and two-space indents,
// and every zip entry gets the same fixed timestamp, so saving the same design
// twice gives the same bytes (good for Git diffs and tests).

namespace kh::io {

namespace {

using model::Document;
using model::Feature;
using model::FeatureType;
using nlohmann::json;

constexpr std::uint64_t kMaxEntryBytes = 64ull * 1024 * 1024;  // refuse absurd entries (zip bombs)
// Every entry is stamped 1980-01-01 00:00, the earliest zip date, written directly in
// zip's DOS format. (zip_file_set_mtime would convert through the local time zone,
// so the same design saved in two time zones would give different bytes.)
constexpr zip_uint16_t kDosTime = 0;                        // 00:00:00
constexpr zip_uint16_t kDosDate = (0 << 9) | (1 << 5) | 1;  // years since 1980, month, day

// ---------------------------------------------------------------------------
// Messages (plain words for students; technical detail goes in `detail`)
// ---------------------------------------------------------------------------

Diagnostic problem(std::string code, std::string message, std::string detail = {}) {
    return {std::move(code), Severity::Error, std::move(message), std::move(detail)};
}

Diagnostic notKhan(std::string_view name, std::string detail) {
    return problem(
        "file.not_khan",
        std::string(name) + " isn't a Khan design, so Khan can't open it. Khan designs end in .khan.",
        std::move(detail));
}

Diagnostic damaged(std::string_view name, std::string detail) {
    return problem("file.damaged",
                   std::string(name) +
                       " is damaged, so Khan can't open it. If you have an older copy, try opening that one.",
                   std::move(detail));
}

// ---------------------------------------------------------------------------
// libzip helpers
// ---------------------------------------------------------------------------

struct ZipCloser {
    void operator()(zip_t* z) const { zip_discard(z); }
};
struct SourceFreer {
    void operator()(zip_source_t* s) const { zip_source_free(s); }
};
using ZipPtr = std::unique_ptr<zip_t, ZipCloser>;
using SourcePtr = std::unique_ptr<zip_source_t, SourceFreer>;

std::string zipErrorText(zip_error_t* err) {
    std::string text = zip_error_strerror(err);
    zip_error_fini(err);
    return text;
}

// Reads one entry as text. Returns nullopt if it is missing or too large.
std::optional<std::string> readEntry(zip_t* za, const std::string& name, std::string& why) {
    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat(za, name.c_str(), 0, &st) != 0) {
        why = "missing " + name;
        return std::nullopt;
    }
    if (!(st.valid & ZIP_STAT_SIZE) || st.size > kMaxEntryBytes) {
        why = name + " is too large";
        return std::nullopt;
    }
    zip_file_t* f = zip_fopen(za, name.c_str(), 0);
    if (!f) {
        why = "cannot open " + name + ": " + zip_strerror(za);
        return std::nullopt;
    }
    std::string text(static_cast<std::size_t>(st.size), '\0');
    const zip_int64_t got = zip_fread(f, text.data(), st.size);
    zip_fclose(f);
    if (got < 0 || static_cast<zip_uint64_t>(got) != st.size) {
        why = "cannot read " + name;
        return std::nullopt;
    }
    return text;
}

// ---------------------------------------------------------------------------
// JSON <-> values
// ---------------------------------------------------------------------------

json vec3(const kernel::Vec3& v) {
    return json::array({v.x, v.y, v.z});
}

std::optional<kernel::Vec3> readVec3(const json& j) {
    if (!j.is_array() || j.size() != 3) return std::nullopt;
    for (const auto& c : j) {
        if (!c.is_number() || !std::isfinite(c.get<double>())) return std::nullopt;
    }
    return kernel::Vec3{j[0].get<double>(), j[1].get<double>(), j[2].get<double>()};
}

std::optional<double> readNumber(const json& j) {
    if (!j.is_number()) return std::nullopt;
    const double d = j.get<double>();
    return std::isfinite(d) ? std::optional<double>(d) : std::nullopt;
}

const char* kindName(model::DocKind k) {
    return k == model::DocKind::Assembly ? "assembly" : "part";
}

const char* opName(kernel::BoolOp op) {
    switch (op) {
        case kernel::BoolOp::Union:
            return "union";
        case kernel::BoolOp::Cut:
            return "cut";
        case kernel::BoolOp::Intersect:
            return "intersect";
    }
    return "union";
}

std::optional<kernel::BoolOp> readOp(const json& j) {
    if (!j.is_string()) return std::nullopt;
    const auto s = j.get<std::string>();
    if (s == "union") return kernel::BoolOp::Union;
    if (s == "cut") return kernel::BoolOp::Cut;
    if (s == "intersect") return kernel::BoolOp::Intersect;
    return std::nullopt;
}

json featureToJson(const Feature& f) {
    json j{{"id", f.id().toString()}, {"name", f.name()}};
    if (!f.material().empty()) j["material"] = f.material();  // left out when not picked
    switch (f.type()) {
        case FeatureType::Box: {
            const auto& b = static_cast<const model::BoxFeature&>(f);
            j["type"] = "Box";
            j["params"] = {{"corner", vec3(b.corner())}, {"size", vec3(b.size())}};
            break;
        }
        case FeatureType::Cylinder: {
            const auto& c = static_cast<const model::CylinderFeature&>(f);
            j["type"] = "Cylinder";
            j["params"] = {{"base", vec3(c.base())},
                           {"axis", vec3(c.axis())},
                           {"radius", c.radius()},
                           {"height", c.height()}};
            break;
        }
        case FeatureType::Boolean: {
            const auto& b = static_cast<const model::BooleanFeature&>(f);
            j["type"] = "Boolean";
            j["inputs"] = {{"a", b.a().toString()}, {"b", b.b().toString()}};
            j["params"] = {{"operation", opName(b.op())}};
            break;
        }
    }
    return j;
}

// Builds a feature from its JSON. On failure sets `why` (technical) and, for an
// unknown type, `unknownType`.
std::unique_ptr<Feature> featureFromJson(const json& j, const FeatureId& id, std::string& why,
                                         std::string& unknownType) {
    if (!j.is_object() || !j.contains("type") || !j["type"].is_string() || !j.contains("name") ||
        !j["name"].is_string()) {
        why = "feature " + id.toString() + " has no type or name";
        return nullptr;
    }
    const auto type = j["type"].get<std::string>();
    const auto name = j["name"].get<std::string>();
    const json params = j.value("params", json::object());
    const json inputs = j.value("inputs", json::object());
    auto bad = [&](const char* what) {
        why = "step " + name + ": bad or missing " + what;
        return nullptr;
    };

    if (type == "Box") {
        const auto corner = readVec3(params.value("corner", json()));
        const auto size = readVec3(params.value("size", json()));
        if (!corner) return bad("corner");
        if (!size) return bad("size");
        return std::make_unique<model::BoxFeature>(id, name, *corner, *size);
    }
    if (type == "Cylinder") {
        const auto base = readVec3(params.value("base", json()));
        const auto axis = readVec3(params.value("axis", json()));
        const auto radius = readNumber(params.value("radius", json()));
        const auto height = readNumber(params.value("height", json()));
        if (!base) return bad("base");
        if (!axis) return bad("axis");
        if (!radius) return bad("radius");
        if (!height) return bad("height");
        return std::make_unique<model::CylinderFeature>(id, name, *base, *axis, *radius, *height);
    }
    if (type == "Boolean") {
        const auto op = readOp(params.value("operation", json()));
        const auto a = inputs.contains("a") && inputs["a"].is_string()
                           ? FeatureId::parse(inputs["a"].get<std::string>())
                           : std::nullopt;
        const auto b = inputs.contains("b") && inputs["b"].is_string()
                           ? FeatureId::parse(inputs["b"].get<std::string>())
                           : std::nullopt;
        if (!op) return bad("operation");
        if (!a) return bad("input a");
        if (!b) return bad("input b");
        return std::make_unique<model::BooleanFeature>(id, name, *op, *a, *b);
    }
    unknownType = type;
    why = "step " + name + " has unknown type " + type;
    return nullptr;
}

json materialToJson(const model::Material& m) {
    return {{"id", m.key},
            {"name", m.name},
            {"density", m.density},
            {"color", model::colorText(m.color)},
            {"youngs_modulus", m.youngsModulus},
            {"poissons_ratio", m.poissonsRatio},
            {"yield_strength", m.yieldStrength},
            {"cost_per_kg", m.costPerKg}};
}

std::optional<model::Material> materialFromJson(const json& j) {
    if (!j.is_object()) return std::nullopt;
    model::Material m;
    const auto key = MaterialId::parse(j.value("id", ""));
    const auto color = model::parseColor(j.value("color", ""));
    const auto density = readNumber(j.value("density", json()));
    if (!key || !color || !density || !j.contains("name") || !j["name"].is_string()) return std::nullopt;
    m.key = key->toString();
    m.name = j["name"].get<std::string>();
    m.density = *density;
    m.color = *color;
    m.youngsModulus = readNumber(j.value("youngs_modulus", json(0))).value_or(0);
    m.poissonsRatio = readNumber(j.value("poissons_ratio", json(0))).value_or(0);
    m.yieldStrength = readNumber(j.value("yield_strength", json(0))).value_or(0);
    m.costPerKg = readNumber(j.value("cost_per_kg", json(0))).value_or(0);
    return m;
}

std::string featurePath(const FeatureId& id) {
    return "features/" + id.toString() + ".json";
}

std::string dump(const json& j) {
    return j.dump(2) + "\n";
}

}  // namespace

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

Diagnostics writeBytes(const Document& doc, Bytes& out) {
    // Build every entry's text first.
    std::deque<std::pair<std::string, std::string>> entries;  // deque: stable addresses for libzip
    json timeline = json::array();
    json files = json::array({"timeline.json"});
    if (!doc.customMaterials().empty()) {
        // Only designs with materials of their own get this file, so other designs
        // save exactly as before.
        json materials = json::array();
        for (const auto& m : doc.customMaterials()) materials.push_back(materialToJson(m));
        entries.emplace_back("materials.json", dump(json{{"materials", materials}}));
        files.push_back("materials.json");
    }
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        const Feature& f = doc.featureAt(i);
        timeline.push_back({{"id", f.id().toString()}, {"suppressed", f.suppressed()}});
        entries.emplace_back(featurePath(f.id()), dump(featureToJson(f)));
        files.push_back(featurePath(f.id()));
    }
    if (const auto folders = doc.folders(); !folders.empty()) {
        // Only designs with folders get this file, so other designs save exactly as before.
        json list = json::array();
        for (const auto& folder : folders) {
            json steps = json::array();
            for (const auto& id : folder.steps) steps.push_back(id.toString());
            list.push_back({{"id", folder.id.toString()}, {"name", folder.name}, {"steps", steps}});
        }
        entries.emplace_back("folders.json", dump(json{{"folders", list}}));
        files.push_back("folders.json");
    }
    const json manifest{
        {"format", "khan"},
        {"schema", kFileSchemaVersion},
        {"app", std::string(version())},
        {"units", {{"length", "mm"}, {"angle", "rad"}}},
        {"document", {{"id", doc.id().toString()}, {"kind", kindName(doc.kind())}, {"name", doc.name()}}},
        {"files", files},
    };
    entries.emplace_front("timeline.json", dump(json{{"features", timeline}}));
    entries.emplace_front("manifest.json", dump(manifest));

    auto failed = [](std::string detail) {
        return Diagnostics{
            problem("file.cant_write", "Khan couldn't put the design into a file.", std::move(detail))};
    };

    zip_error_t err;
    zip_error_init(&err);
    SourcePtr src(zip_source_buffer_create(nullptr, 0, 0, &err));
    if (!src) return failed(zipErrorText(&err));
    ZipPtr za(zip_open_from_source(src.get(), ZIP_TRUNCATE, &err));
    if (!za) return failed(zipErrorText(&err));
    zip_source_keep(src.get());  // za now shares src; keep it alive after closing za

    for (const auto& [name, text] : entries) {
        zip_source_t* s = zip_source_buffer(za.get(), text.data(), text.size(), 0);
        if (!s) return failed(zip_strerror(za.get()));
        const zip_int64_t index =
            zip_file_add(za.get(), name.c_str(), s, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE);
        if (index < 0) {
            zip_source_free(s);
            return failed(zip_strerror(za.get()));
        }
        if (zip_file_set_dostime(za.get(), static_cast<zip_uint64_t>(index), kDosTime, kDosDate, 0) != 0) {
            return failed(zip_strerror(za.get()));
        }
    }
    if (zip_close(za.get()) != 0) return failed(zip_strerror(za.get()));
    (void)za.release();  // zip_close freed it

    // Copy the finished archive out of the memory source.
    if (zip_source_open(src.get()) != 0) return failed("cannot reopen archive buffer");
    zip_source_seek(src.get(), 0, SEEK_END);
    const zip_int64_t size = zip_source_tell(src.get());
    zip_source_seek(src.get(), 0, SEEK_SET);
    out.assign(static_cast<std::size_t>(size < 0 ? 0 : size), 0);
    const zip_int64_t got = zip_source_read(src.get(), out.data(), out.size());
    zip_source_close(src.get());
    if (size < 0 || got != size) return failed("cannot read archive buffer");
    return {};
}

Diagnostics saveFile(Document& doc, const std::filesystem::path& path) {
    auto issues = writeFile(doc, path);
    if (issues.empty()) doc.markSaved();
    return issues;
}

Diagnostics writeFile(const Document& doc, const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    auto cantWrite = [&](std::string detail) {
        return Diagnostics{problem("file.cant_write",
                                   "Khan couldn't save " + name +
                                       ". Check that the folder exists, that you're allowed to save there, "
                                       "and that the disk isn't full.",
                                   std::move(detail))};
    };

    Bytes bytes;
    if (auto issues = writeBytes(doc, bytes); !issues.empty()) return issues;

    // Write next to the target, then move into place, so a failed save never
    // leaves a half-written design behind.
    auto tmp = path;
    tmp += ".saving";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return cantWrite("cannot create " + tmp.string());
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        f.close();
        if (!f) {
            std::error_code ignore;
            std::filesystem::remove(tmp, ignore);
            return cantWrite("cannot write " + tmp.string());
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::error_code ignore;
        std::filesystem::remove(tmp, ignore);
        return cantWrite("cannot replace " + path.string() + ": " + ec.message());
    }
    return {};
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

OpenResult readBytes(const Bytes& data, std::string_view displayName) {
    OpenResult result;
    auto fail = [&](Diagnostic d) {
        result.issues.push_back(std::move(d));
        return std::move(result);
    };

    zip_error_t err;
    zip_error_init(&err);
    zip_source_t* raw = zip_source_buffer_create(data.data(), data.size(), 0, &err);
    if (!raw) return fail(notKhan(displayName, zipErrorText(&err)));
    ZipPtr za(zip_open_from_source(raw, ZIP_RDONLY, &err));
    if (!za) {
        zip_source_free(raw);
        return fail(notKhan(displayName, zipErrorText(&err)));
    }

    std::string why;
    auto readJson = [&](const std::string& entry) -> std::optional<json> {
        auto text = readEntry(za.get(), entry, why);
        if (!text) return std::nullopt;
        json j = json::parse(*text, nullptr, false);
        if (j.is_discarded()) {
            why = entry + " is not valid JSON";
            return std::nullopt;
        }
        return std::optional<json>(std::in_place,
                                   std::move(j));  // not "return j": ambiguous with nlohmann 3.12
    };

    // Manifest: is this a Khan file, and can this version read it?
    const auto manifest = readJson("manifest.json");
    if (!manifest) {
        return fail(why.rfind("missing", 0) == 0 ? notKhan(displayName, why) : damaged(displayName, why));
    }
    if (!manifest->is_object() || manifest->value("format", "") != "khan") {
        return fail(notKhan(displayName, "manifest format is not \"khan\""));
    }
    const auto schema = manifest->value("schema", -1);
    if (schema > kFileSchemaVersion) {
        return fail(problem(
            "file.too_new",
            std::string(displayName) + " was saved by a newer version of Khan. Update Khan to open it.",
            "file schema " + std::to_string(schema) + ", this Khan reads up to schema " +
                std::to_string(kFileSchemaVersion)));
    }
    if (schema < 1) return fail(damaged(displayName, "bad schema number"));

    const json docInfo = manifest->value("document", json::object());
    const auto docId = DocId::parse(docInfo.value("id", ""));
    if (!docId) return fail(damaged(displayName, "manifest has no document id"));
    const auto kind =
        docInfo.value("kind", "part") == "assembly" ? model::DocKind::Assembly : model::DocKind::Part;
    auto doc = std::make_unique<Document>(*docId, kind, docInfo.value("name", std::string(displayName)));

    // The design's own materials, before the steps that use them.
    // (A design without materials of its own has no materials.json.)
    if (const auto materials = readJson("materials.json"); materials) {
        const json list = materials->value("materials", json());
        if (!list.is_array()) return fail(damaged(displayName, "materials.json has no material list"));
        for (const auto& mj : list) {
            auto m = materialFromJson(mj);
            if (!m)
                return fail(damaged(displayName,
                                    "a material in materials.json is missing its id, name, "
                                    "density or color"));
            const auto name = m->name;
            if (auto issues = doc->execute(std::make_unique<model::DefineMaterialCommand>(std::move(*m)));
                !issues.empty()) {
                return fail(damaged(displayName, "material " + name + ": " + issues.front().code));
            }
        }
    } else if (why.rfind("missing", 0) != 0) {
        return fail(damaged(displayName, why));  // there, but not readable
    }

    // Timeline: the features in order.
    const auto timeline = readJson("timeline.json");
    if (!timeline) return fail(damaged(displayName, why));
    const json order = timeline->value("features", json());
    if (!order.is_array()) return fail(damaged(displayName, "timeline has no feature list"));

    for (const auto& entry : order) {
        const auto id = entry.is_object() ? FeatureId::parse(entry.value("id", "")) : std::nullopt;
        if (!id) return fail(damaged(displayName, "timeline entry without a valid id"));
        const auto fj = readJson(featurePath(*id));
        if (!fj) return fail(damaged(displayName, why));

        std::string unknownType;
        auto feature = featureFromJson(*fj, *id, why, unknownType);
        if (!feature) {
            if (!unknownType.empty()) {
                return fail(problem("file.unknown_step",
                                    std::string(displayName) + " has a step, " + fj->value("name", "?") +
                                        ", of a kind this version of Khan doesn't know (" + unknownType +
                                        "). It may need a newer Khan or a plugin.",
                                    why));
            }
            return fail(damaged(displayName, why));
        }
        feature->setSuppressed(entry.value("suppressed", false));
        if (fj->contains("material") && (*fj)["material"].is_string()) {
            // A key this Khan doesn't know (a material from a newer Khan) is kept,
            // and the body counts as the default material until it's changed.
            feature->setMaterial((*fj)["material"].get<std::string>());
        }

        // Add through the normal command, so the same checks run as when a
        // student builds the design (unique names, inputs come first).
        const auto name = feature->name();
        auto issues = doc->execute(std::make_unique<model::AddFeatureCommand>(std::move(feature)));
        if (!issues.empty()) {
            return fail(damaged(displayName, "step " + name + ": " + issues.front().code));
        }
    }

    // Folders, after the steps they hold, through the same command a student uses.
    // (A design without folders has no folders.json.)
    if (const auto folders = readJson("folders.json"); folders) {
        const json list = folders->value("folders", json());
        if (!list.is_array()) return fail(damaged(displayName, "folders.json has no folder list"));
        for (const auto& fj : list) {
            const auto id = fj.is_object() ? FolderId::parse(fj.value("id", "")) : std::nullopt;
            const json steps = fj.is_object() ? fj.value("steps", json()) : json();
            if (!id || !fj.contains("name") || !fj["name"].is_string() || !steps.is_array()) {
                return fail(
                    damaged(displayName, "a folder in folders.json is missing its id, name or steps"));
            }
            std::vector<FeatureId> ids;
            for (const auto& s : steps) {
                const auto step = s.is_string() ? FeatureId::parse(s.get<std::string>()) : std::nullopt;
                if (!step) return fail(damaged(displayName, "a folder lists a step without a valid id"));
                ids.push_back(*step);
            }
            const std::string name = fj["name"].get<std::string>();
            if (auto issues =
                    doc->execute(std::make_unique<model::AddFolderCommand>(std::move(ids), name, *id));
                !issues.empty()) {
                return fail(damaged(displayName, "folder " + name + ": " + issues.front().code));
            }
        }
    } else if (why.rfind("missing", 0) != 0) {
        return fail(damaged(displayName, why));  // there, but not readable
    }

    doc->clearHistory();  // opening a file is not an undoable step
    doc->markSaved();
    result.document = std::move(doc);
    return result;
}

OpenResult openFile(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        OpenResult r;
        r.issues.push_back(
            problem("file.cant_read",
                    "Khan couldn't open " + name +
                        ". Check that the file is still there and that you're allowed to open it.",
                    path.string()));
        return r;
    }
    Bytes data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return readBytes(data, name);
}

// ---------------------------------------------------------------------------
// Steps as text (the clipboard)
// ---------------------------------------------------------------------------

namespace {
constexpr const char* kStepsFormat = "khan-steps";
}  // namespace

std::string stepsToText(const Document& doc, const std::vector<FeatureId>& ids) {
    // copySteps() finds every step they use, in design order; it gives them new
    // ids, which is harmless here (stepsFromText() gives new ones again).
    const auto steps = model::copySteps(doc, ids);
    if (steps.empty()) return {};
    json list = json::array();
    std::vector<std::string> used;
    for (const auto& f : steps) {
        json j = featureToJson(*f);
        if (f->suppressed()) j["suppressed"] = true;
        list.push_back(j);
        if (!f->material().empty()) used.push_back(f->material());
    }
    json materials = json::array();
    for (const auto& m : doc.customMaterials()) {
        if (std::find(used.begin(), used.end(), m.key) != used.end()) materials.push_back(materialToJson(m));
    }
    return dump(json{{"format", kStepsFormat}, {"version", 1}, {"steps", list}, {"materials", materials}});
}

bool looksLikeSteps(std::string_view text) {
    return text.find(std::string("\"") + kStepsFormat + "\"") != std::string_view::npos &&
           text.find_first_not_of(" \t\r\n") != std::string_view::npos &&
           text[text.find_first_not_of(" \t\r\n")] == '{';
}

StepsResult stepsFromText(std::string_view text) {
    StepsResult result;
    auto fail = [&](std::string code, std::string message, std::string detail = {}) {
        result.steps.clear();
        result.materials.clear();
        result.issues.push_back(problem(std::move(code), std::move(message), std::move(detail)));
        return std::move(result);
    };
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || j.value("format", "") != kStepsFormat) {
        return fail("paste.not_steps",
                    "What's on the clipboard isn't steps copied from Khan. Copy steps in "
                    "the feature tree first (Edit > Copy), then paste.");
    }
    if (j.value("version", 0) > 1) {
        return fail("paste.too_new", "These steps were copied from a newer Khan. Update Khan to paste them.");
    }
    const json steps = j.value("steps", json());
    if (!steps.is_array() || steps.empty()) return fail("paste.not_steps", "There are no steps to paste.");

    for (const auto& mj : j.value("materials", json::array())) {
        if (auto m = materialFromJson(mj)) result.materials.push_back(*m);
    }
    std::vector<std::unique_ptr<Feature>> read;
    std::vector<FeatureId> seen;
    for (const auto& fj : steps) {
        const auto id = fj.is_object() && fj.contains("id") && fj["id"].is_string()
                            ? FeatureId::parse(fj["id"].get<std::string>())
                            : std::nullopt;
        if (!id)
            return fail("paste.damaged", "The copied steps are damaged. Copy them again.",
                        "a step has no id");
        std::string why, unknownType;
        auto f = featureFromJson(fj, *id, why, unknownType);
        if (!f) {
            if (!unknownType.empty()) {
                return fail("paste.unknown_step",
                            "One of the copied steps is a kind this Khan doesn't know (" + unknownType +
                                "). It may need a newer Khan or a plugin.",
                            why);
            }
            return fail("paste.damaged", "The copied steps are damaged. Copy them again.", why);
        }
        // Every input must be an earlier copied step: pasting never reaches into the design.
        for (const auto& dep : f->dependencies()) {
            if (std::find(seen.begin(), seen.end(), dep) == seen.end()) {
                return fail("paste.damaged", "The copied steps are damaged. Copy them again.",
                            "step " + f->name() + " uses a step that wasn't copied");
            }
        }
        f->setSuppressed(fj.value("suppressed", false));
        if (fj.contains("material") && fj["material"].is_string())
            f->setMaterial(fj["material"].get<std::string>());
        seen.push_back(*id);
        read.push_back(std::move(f));
    }
    result.steps = model::freshIds(std::move(read));
    return result;
}

}  // namespace kh::io
