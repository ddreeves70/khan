// kh_cli: headless tool for scripting, smoke tests and the geometry regression corpus.
//
//   kh_cli --version
//   kh_cli demo <out.stl|out.step|out.khan> box with a hole, as a toolchain smoke test
//   kh_cli open <file.khan> [--export out [--step name]]  open a design, rebuild it, report each step
//   kh_cli open <file.khan> --sketch <name>               ... and list a sketch, solved
//   kh_cli materials [<file.khan>]      list materials (Khan's own, and a design's)
//   kh_cli regress [--update] <case|dir>... run regression cases (see tests/corpus/README.md)
//   kh_cli plugins [--plugins <folder>]... [--slow-start-ms <n>] [--strict]  list plugins, their
//                                       commands, services, problems and warnings
//   kh_cli help [--plugins <folder>]... [<command>]  list commands, or describe one
//   kh_cli run [--plugins <folder>]... [--open in.khan] [--save out.khan] <command> [name=value]...

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "Plugins.h"
#include "Regression.h"
#include "kh/core/Version.h"
#include "kh/io/KhanFile.h"
#include "kh/kernel_occt/OcctKernel.h"
#include "kh/model/Commands.h"
#include "kh/model/Document.h"
#include "kh/sketch/Profiles.h"
#include "kh/sketch_planegcs/PlaneGcsSolver.h"

namespace {

int usage() {
    std::puts(
        "usage: kh_cli --version\n"
        "       kh_cli demo <out.stl|out.step|out.khan>\n"
        "       kh_cli open <file.khan> [--export <out.stl|out.step> [--step <name>]]\n"
        "       kh_cli open <file.khan> --sketch <name>\n"
        "       kh_cli materials [<file.khan>]\n"
        "       kh_cli regress [--update] <case.json|folder>...\n"
        "       kh_cli plugins [--plugins <folder>]... [--slow-start-ms <n>] [--strict]\n"
        "       kh_cli help [--plugins <folder>]... [<command>]\n"
        "       kh_cli run [--plugins <folder>]... [--open <in.khan>] [--save <out.khan>] <command> "
        "[name=value]...");
    return 2;
}

void printIssues(const kh::Diagnostics& issues) {
    for (const auto& d : issues) {
        std::fprintf(stderr, "error [%s]: %s %s\n", d.code.c_str(), d.message.c_str(), d.detail.c_str());
    }
}

kh::kernel::ExportFormat formatFor(const std::filesystem::path& out) {
    const auto ext = out.extension().string();
    return (ext == ".step" || ext == ".stp") ? kh::kernel::ExportFormat::Step : kh::kernel::ExportFormat::Stl;
}

// The demo part as a design: Plate, Hole, then Tag = Plate cut Hole.
int demoKhan(const std::filesystem::path& out) {
    using namespace kh;
    model::Document doc(model::DocKind::Part, "Demo tag");
    auto plate = std::make_unique<model::BoxFeature>("Plate", kernel::Vec3{0, 0, 0}, kernel::Vec3{60, 30, 4});
    auto hole = std::make_unique<model::CylinderFeature>("Hole", kernel::Vec3{9, 15, -1},
                                                         kernel::Vec3{0, 0, 1}, 2.5, 6);
    auto tag = std::make_unique<model::BooleanFeature>("Tag", kernel::BoolOp::Cut, plate->id(), hole->id());
    std::vector<std::unique_ptr<model::Feature>> steps;
    steps.push_back(std::move(plate));
    steps.push_back(std::move(hole));
    steps.push_back(std::move(tag));
    for (auto& f : steps) {
        if (auto issues = doc.execute(std::make_unique<model::AddFeatureCommand>(std::move(f)));
            !issues.empty()) {
            printIssues(issues);
            return 1;
        }
    }
    if (auto issues = io::saveFile(doc, out); !issues.empty()) {
        printIssues(issues);
        return 1;
    }
    std::printf("wrote %s\n", out.string().c_str());
    return 0;
}

const char* statusName(kh::model::FeatureStatus s) {
    switch (s) {
        case kh::model::FeatureStatus::Ok:
            return "ok";
        case kh::model::FeatureStatus::Failed:
            return "failed";
        case kh::model::FeatureStatus::Blocked:
            return "blocked";
        case kh::model::FeatureStatus::Suppressed:
            return "off";
    }
    return "?";
}

// A number for printing: 2 decimals, and never "-0.00".
std::string mm(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", std::abs(v) < 0.005 ? 0.0 : v);
    return buf;
}

// A length for printing, in the design's unit: as many decimals as the unit
// shows (2 for mm), and never "-0".
std::string length(double v, kh::LengthUnit unit) {
    const int decimals = kh::unitDecimals(unit);
    const double shown = kh::toUnit(v, unit);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", decimals,
                  std::abs(shown) < 0.5 * std::pow(10.0, -decimals) ? 0.0 : shown);
    return buf;
}

// Lists a sketch: each entity where the solver put it, then each constraint.
// Lengths are in the design's unit.
void printSketch(const kh::model::SketchFeature& step, const kh::model::FeatureResult& result,
                 kh::LengthUnit unit) {
    auto xy = [unit](kh::sketch::Point2 p) { return length(p.x, unit) + ", " + length(p.y, unit); };
    const std::string symbol(kh::unitSymbol(unit));
    namespace ks = kh::sketch;
    const auto& drawn = step.sketch();
    const auto* solved = result.sketch.get();
    const ks::Sketch shown{solved && solved->ok() ? solved->entities : drawn.entities, drawn.constraints};
    const char* plane = step.plane() == kh::model::SketchPlane::XY   ? "XY"
                        : step.plane() == kh::model::SketchPlane::XZ ? "XZ"
                                                                     : "YZ";
    std::string state = "not solved";
    if (solved && solved->ok()) {
        state = solved->freedom == 0 ? "fully constrained"
                                     : "free to move " + std::to_string(solved->freedom) +
                                           (solved->freedom == 1 ? " way" : " ways");
    } else if (solved && solved->status == ks::SolveStatus::Conflicting) {
        state = "constraints conflict";
    }
    std::printf("%s (%s plane): %s\n", step.name().c_str(), plane, state.c_str());
    for (const auto& e : shown.entities) {
        std::string what;
        if (const auto* p = std::get_if<ks::PointShape>(&e.shape)) {
            what = "point   " + xy(p->at);
        } else if (const auto* l = std::get_if<ks::LineShape>(&e.shape)) {
            what = "line    " + xy(l->start) + " to " + xy(l->end);
        } else if (const auto* c = std::get_if<ks::CircleShape>(&e.shape)) {
            what = "circle  center " + xy(c->center) + "  radius " + length(c->radius, unit);
        } else if (const auto* sp = std::get_if<ks::SplineShape>(&e.shape)) {
            // "spline  degree 3, open  poles 0.00, 0.00 / 0.00, 30.00 / ..."
            std::string poles;
            for (const auto& pole : sp->poles) poles += (poles.empty() ? "" : " / ") + xy(pole);
            what = "spline  degree " + std::to_string(sp->degree) + (sp->closed ? ", closed" : ", open") +
                   "  poles " + poles;
            if (!sp->weights.empty()) {
                std::string w;
                for (double x : sp->weights) w += (w.empty() ? "" : ", ") + mm(x);
                what += "  weights " + w;
            }
        } else if (const auto* a = std::get_if<ks::ArcShape>(&e.shape)) {
            what = "arc     center " + xy(a->center) + "  radius " + length(a->radius, unit) + "  from " +
                   mm(a->startAngle * 180 / std::numbers::pi) + " to " +
                   mm(a->endAngle * 180 / std::numbers::pi) + " degrees";
        }
        std::printf("  %-14s %s%s\n", e.name.c_str(), what.c_str(), e.construction ? "  (construction)" : "");
    }
    auto has = [](const std::vector<kh::ConstraintId>& ids, const kh::ConstraintId& id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };
    for (const auto& c : shown.constraints) {
        std::string on;
        auto add = [&on](const std::string& part) { on += (on.empty() ? "" : ", ") + part; };
        for (const auto& p : c.points) {
            const auto* e = shown.find(p.entity);
            const std::string role = ks::pointKey(p);
            add((e ? e->name : "?") + (role.empty() ? "" : "." + role));
        }
        for (const auto& id : c.entities) {
            const auto* e = shown.find(id);
            add(e ? e->name : "?");
        }
        std::string value;
        if (c.kind == ks::ConstraintKind::Angle) {
            value = "  " + mm(c.value * 180 / std::numbers::pi) + " degrees";
        } else if (ks::isDimension(c.kind)) {
            value = "  " + length(c.value, unit) + " " + symbol;
        }
        std::string note;
        if (solved && has(solved->conflicting, c.id)) note = "  (conflicts)";
        if (solved && has(solved->redundant, c.id)) note = "  (repeats another)";
        std::printf("  %-14s %-19s %s%s%s\n", c.name.c_str(), std::string(ks::kindKey(c.kind)).c_str(),
                    on.c_str(), value.c_str(), note.c_str());
    }
    // Closed shapes (what extrude will use), and ends that aren't joined yet.
    const bool anyEdges = std::any_of(shown.entities.begin(), shown.entities.end(), [](const ks::Entity& e) {
        return !e.construction && !std::holds_alternative<ks::PointShape>(e.shape);
    });
    if (!anyEdges) return;
    const auto found = ks::findProfiles(shown);
    auto names = [&](const ks::Loop& loop) {
        std::string out;
        for (const auto& e : loop.edges) {
            const auto* x = shown.find(e.entity);
            out += (out.empty() ? "" : ", ") + (x ? x->name : "?");
        }
        return out;
    };
    std::printf("closed shapes: %zu\n", found.shapes.size());
    for (std::size_t i = 0; i < found.shapes.size(); ++i) {
        const auto& p = found.shapes[i];
        std::string holes;
        for (const auto& h : p.holes) holes += (holes.empty() ? "  holes: " : "; ") + names(h);
        const double per = kh::mmPerUnit(unit);
        std::printf("  shape %zu  %s  area %.2f %s^2%s\n", i + 1, names(p.outer).c_str(),
                    p.area() / (per * per), symbol.c_str(), holes.c_str());
    }
    if (!found.openEnds.empty()) {
        std::string ends;
        for (const auto& r : found.openEnds) {
            const auto* x = shown.find(r.entity);
            ends += (ends.empty() ? "" : ", ") + (x ? x->name : "?") + "." + ks::pointKey(r);
        }
        std::printf("open ends: %s\n", ends.c_str());
    }
    for (const auto& d : found.issues) std::printf("warning [%s]: %s\n", d.code.c_str(), d.message.c_str());
}

// Opens a design, rebuilds it and prints one line per step. Returns 1 if the
// file can't be opened or any active step fails.
int openDesign(int argc, char** argv) {
    const bool exporting = argc >= 5 && std::string_view(argv[3]) == "--export";
    const bool oneStep = argc == 7 && exporting && std::string_view(argv[5]) == "--step";
    const bool showSketch = argc == 5 && std::string_view(argv[3]) == "--sketch";
    if (argc != 3 && !(argc == 5 && exporting) && !oneStep && !showSketch) return usage();
    auto opened = kh::io::openFile(argv[2]);
    if (!opened.ok()) {
        printIssues(opened.issues);
        return 1;
    }
    const auto& doc = *opened.document;
    auto kernel = kh::kernel::makeOcctKernel();
    auto solver = kh::sketch::makePlaneGcsSolver();
    const auto report = doc.regenerate(*kernel, solver.get());

    std::printf("%s: %zu %s\n", doc.name().c_str(), doc.featureCount(),
                doc.featureCount() == 1 ? "step" : "steps");
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        const auto& f = doc.featureAt(i);
        const auto* r = report.result(f.id());
        // A folder's name comes before its first step, and its steps are indented.
        const auto* folder = doc.findFolder(doc.folderOf(f.id()));
        if (folder && folder->steps.front() == f.id()) std::printf("  %s/\n", folder->name.c_str());
        std::printf(folder ? "    %-10s %s" : "  %-12s %s", f.name().c_str(), statusName(r->status));
        if (r->sketch && r->status == kh::model::FeatureStatus::Ok) {
            std::printf("  %s",
                        kh::cli::sketchSummary(static_cast<const kh::model::SketchFeature&>(f).sketch(),
                                               r->sketch->freedom)
                            .c_str());
        } else if (r->status == kh::model::FeatureStatus::Ok) {
            const auto& material = doc.materialOf(f.id());
            const auto mass = kh::model::bodyMass(*kernel, r->shape, material);
            std::printf("  volume %s", kh::volumeText(mass.volume, doc.lengthUnit()).c_str());
            // Finished bodies also get their mass and material.
            const auto& bodies = report.bodies();
            if (std::find(bodies.begin(), bodies.end(), f.id()) != bodies.end()) {
                std::printf("  mass %.2f g (%s)", mass.grams, material.name.c_str());
            }
        }
        std::printf("\n");
        printIssues(r->issues);
    }
    kh::cli::printParameters(doc);
    if (showSketch) {
        const kh::model::Feature* found = nullptr;
        for (std::size_t i = 0; i < doc.featureCount(); ++i) {
            if (doc.featureAt(i).name() == argv[4]) found = &doc.featureAt(i);
        }
        if (!found || found->type() != kh::model::FeatureType::Sketch) {
            std::fprintf(stderr, "error: this design has no sketch called %s.\n", argv[4]);
            return 1;
        }
        printSketch(static_cast<const kh::model::SketchFeature&>(*found), *report.result(found->id()),
                    doc.lengthUnit());
    }
    if (!report.ok()) return 1;

    if (exporting) {
        const std::filesystem::path out = argv[4];
        const kh::kernel::ShapeHandle* shape = nullptr;
        if (oneStep) {
            // Export the shape of one named step, even if later steps use it.
            const std::string_view stepName = argv[6];
            for (std::size_t i = 0; i < doc.featureCount(); ++i) {
                if (doc.featureAt(i).name() == stepName) shape = report.shapeOf(doc.featureAt(i).id());
            }
            if (!shape) {
                std::fprintf(stderr, "error: this design has no step called %s.\n", argv[6]);
                return 1;
            }
        } else if (report.bodies().size() != 1) {
            std::fprintf(stderr,
                         "error: this design has %zu finished bodies, and export writes one. "
                         "Combine them with feature.add_boolean, or pick one with --step <name>.\n",
                         report.bodies().size());
            return 1;
        } else {
            shape = report.shapeOf(report.bodies().front());
        }
        auto issues = kernel->exportFile(*shape, formatFor(out), out);
        if (!issues.empty()) {
            printIssues(issues);
            return 1;
        }
        std::printf("wrote %s\n", out.string().c_str());
    }
    return 0;
}

// Lists Khan's materials, and a design's own ones after them.
int materials(int argc, char** argv) {
    if (argc > 3) return usage();
    std::vector<kh::model::Material> list = kh::model::builtinMaterials();
    const std::size_t builtinCount = list.size();
    std::unique_ptr<kh::model::Document> doc;
    if (argc == 3) {
        auto opened = kh::io::openFile(argv[2]);
        if (!opened.ok()) {
            printIssues(opened.issues);
            return 1;
        }
        doc = std::move(opened.document);
        list.insert(list.end(), doc->customMaterials().begin(), doc->customMaterials().end());
    }
    auto num = [](double v, const char* format) {
        if (v == 0) return std::string("-");
        char buf[32];
        std::snprintf(buf, sizeof buf, format, v);
        return std::string(buf);
    };
    std::printf("%-22s %8s %9s %8s %8s %7s\n", "material", "density", "stiffness", "poisson", "yield",
                "price");
    std::printf("%-22s %8s %9s %8s %8s %7s\n", "", "g/cm^3", "GPa", "", "MPa", "per kg");
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (i == builtinCount) std::printf("%s's own:\n", doc->name().c_str());
        const auto& m = list[i];
        std::printf("%-22s %8s %9s %8s %8s %7s\n", m.name.c_str(), num(m.density, "%.2f").c_str(),
                    num(m.youngsModulus, "%.2f").c_str(), num(m.poissonsRatio, "%.2f").c_str(),
                    num(m.yieldStrength, "%.0f").c_str(), num(m.costPerKg, "%.2f").c_str());
    }
    return 0;
}

int demo(const std::filesystem::path& out) {
    using namespace kh::kernel;
    if (out.extension() == ".khan") return demoKhan(out);
    auto kernel = makeOcctKernel();

    auto box = kernel->makeBox({0, 0, 0}, 60, 30, 4);
    auto hole = kernel->makeCylinder({9, 15, -1}, {0, 0, 1}, 2.5, 6);
    if (!box.ok() || !hole.ok()) {
        printIssues(box.issues);
        printIssues(hole.issues);
        return 1;
    }
    auto tag = kernel->boolean(BoolOp::Cut, box.shape, hole.shape);
    if (!tag.ok()) {
        printIssues(tag.issues);
        return 1;
    }

    const auto mp = kernel->massProps(tag.shape);
    std::printf("volume: %.2f mm^3\n", mp.volume);

    auto issues = kernel->exportFile(tag.shape, formatFor(out), out);
    if (!issues.empty()) {
        printIssues(issues);
        return 1;
    }
    std::printf("wrote %s\n", out.string().c_str());
    return 0;
}

int regress(int argc, char** argv) {
    kh::cli::RegressionOptions options;
    std::vector<std::filesystem::path> paths;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--update") {
            options.update = true;
        } else {
            paths.emplace_back(argv[i]);
        }
    }
    if (paths.empty()) return usage();
    auto kernel = kh::kernel::makeOcctKernel();
    return kh::cli::runRegression(*kernel, paths, options);
}

}  // namespace

int main(int argc, char** argv) {
    // No buffering, so normal output and error messages come out in the order
    // they happen (the CLI reference's examples are checked against it).
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) return usage();
    const std::string_view cmd = argv[1];
    if (cmd == "--version") {
        std::printf("kh_cli %.*s\n", static_cast<int>(kh::version().size()), kh::version().data());
        return 0;
    }
    if (cmd == "demo" && argc == 3) return demo(argv[2]);
    if (cmd == "open") return openDesign(argc, argv);
    if (cmd == "materials") return materials(argc, argv);
    if (cmd == "regress") return regress(argc, argv);
    if (cmd == "plugins") return kh::cli::listPlugins(argc, argv);
    if (cmd == "help") return kh::cli::help(argc, argv);
    if (cmd == "run") return kh::cli::runCommand(argc, argv);
    return usage();
}
