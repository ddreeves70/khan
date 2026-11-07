// sketchbillet_cli: headless tool for scripting, smoke tests and the geometry regression corpus.
//
//   sketchbillet_cli --version
//   sketchbillet_cli demo <out.stl|out.step|out.sb> box with a hole, as a toolchain smoke test
//   sketchbillet_cli open <file.sb> [--export out [--step name]]  open a design, rebuild it, report each step
//   sketchbillet_cli open <file.sb> --sketch <name>               ... and list a sketch, solved
//   sketchbillet_cli open <file.sb> --edges|--faces <name>        ... and list a body's edges or faces,
//   numbered sketchbillet_cli materials [<file.sb>]      list materials (SketchBillet's own, and a design's)
//   sketchbillet_cli regress [--update] <case|dir>... run regression cases (see tests/corpus/README.md)
//   sketchbillet_cli plugins [--plugins <folder>]... [--slow-start-ms <n>] [--strict]  list plugins, their
//                                       commands, services, problems and warnings
//   sketchbillet_cli help [--plugins <folder>]... [<command>]  list commands, or describe one
//   sketchbillet_cli run [--plugins <folder>]... [--open in.sb] [--save out.sb] <command> [name=value]...

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
#include "sb/core/Version.h"
#include "sb/io/SketchBilletFile.h"
#include "sb/kernel_occt/OcctKernel.h"
#include "sb/model/Commands.h"
#include "sb/model/Document.h"
#include "sb/sketch/Profiles.h"
#include "sb/sketch_planegcs/PlaneGcsSolver.h"

namespace {

int usage() {
    std::puts(
        "usage: sketchbillet_cli --version\n"
        "       sketchbillet_cli demo <out.stl|out.step|out.sb>\n"
        "       sketchbillet_cli open <file.sb> [--export <out.stl|out.step> [--step <name>]]\n"
        "       sketchbillet_cli open <file.sb> --sketch <name>\n"
        "       sketchbillet_cli open <file.sb> --edges <name>\n"
        "       sketchbillet_cli open <file.sb> --faces <name>\n"
        "       sketchbillet_cli materials [<file.sb>]\n"
        "       sketchbillet_cli regress [--update] <case.json|folder>...\n"
        "       sketchbillet_cli plugins [--plugins <folder>]... [--slow-start-ms <n>] [--strict]\n"
        "       sketchbillet_cli help [--plugins <folder>]... [<command>]\n"
        "       sketchbillet_cli run [--plugins <folder>]... [--open <in.sb>] [--save <out.sb>] <command> "
        "[name=value]...");
    return 2;
}

void printIssues(const sb::Diagnostics& issues) {
    for (const auto& d : issues) {
        std::fprintf(stderr, "error [%s]: %s %s\n", d.code.c_str(), d.message.c_str(), d.detail.c_str());
    }
}

sb::kernel::ExportFormat formatFor(const std::filesystem::path& out) {
    const auto ext = out.extension().string();
    return (ext == ".step" || ext == ".stp") ? sb::kernel::ExportFormat::Step : sb::kernel::ExportFormat::Stl;
}

// The demo part as a design: Plate, Hole, then Tag = Plate cut Hole.
int demoSketchBillet(const std::filesystem::path& out) {
    using namespace sb;
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

const char* statusName(sb::model::FeatureStatus s) {
    switch (s) {
        case sb::model::FeatureStatus::Ok:
            return "ok";
        case sb::model::FeatureStatus::Failed:
            return "failed";
        case sb::model::FeatureStatus::Blocked:
            return "blocked";
        case sb::model::FeatureStatus::Suppressed:
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
std::string length(double v, sb::LengthUnit unit) {
    const int decimals = sb::unitDecimals(unit);
    const double shown = sb::toUnit(v, unit);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", decimals,
                  std::abs(shown) < 0.5 * std::pow(10.0, -decimals) ? 0.0 : shown);
    return buf;
}

// Lists a sketch: each entity where the solver put it, then each constraint.
// Lengths are in the design's unit.
void printSketch(const sb::model::SketchFeature& step, const sb::model::FeatureResult& result,
                 sb::LengthUnit unit) {
    auto xy = [unit](sb::sketch::Point2 p) { return length(p.x, unit) + ", " + length(p.y, unit); };
    const std::string symbol(sb::unitSymbol(unit));
    namespace ks = sb::sketch;
    const auto& drawn = step.sketch();
    const auto* solved = result.sketch.get();
    const ks::Sketch shown{solved && solved->ok() ? solved->entities : drawn.entities, drawn.constraints};
    const char* plane = step.plane() == sb::model::SketchPlane::XY   ? "XY"
                        : step.plane() == sb::model::SketchPlane::XZ ? "XZ"
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
    auto has = [](const std::vector<sb::ConstraintId>& ids, const sb::ConstraintId& id) {
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
        if (!c.expression.empty()) value += " = " + c.expression;
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
        const double per = sb::mmPerUnit(unit);
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

const char* geomWord(sb::kernel::GeomKind g) {
    using G = sb::kernel::GeomKind;
    switch (g) {
        case G::Plane:
            return "flat";
        case G::Cylinder:
            return "cylinder";
        case G::Cone:
            return "cone";
        case G::Sphere:
            return "ball";
        case G::Torus:
            return "ring";
        case G::Line:
            return "line";
        case G::Circle:
            return "circle";
        case G::Ellipse:
            return "ellipse";
        case G::Spline:
            return "spline";
        case G::Point:
            return "point";
        case G::Other:
            return "curved";
    }
    return "?";
}

// Lists a body's edges or faces, numbered from 1 in listing order (bottom to top,
// front to back, left to right), as feature.add_fillet's edges= takes them.
void printElements(const sb::model::Document& doc, const sb::model::Feature& step,
                   const sb::model::ShapeNames& names, sb::kernel::TopoKind kind) {
    const auto unit = doc.lengthUnit();
    const std::string symbol(sb::unitSymbol(unit));
    const bool faces = kind == sb::kernel::TopoKind::Face;
    const auto& infos = names.elements.of(kind);
    std::printf("%s of %s: %zu\n", faces ? "faces" : "edges", step.name().c_str(), infos.size());
    const auto order = sb::model::listingOrder(names.elements, kind);
    for (std::size_t i = 0; i < order.size(); ++i) {
        const auto k = static_cast<std::size_t>(order[i]);
        const auto& e = infos[k];
        const double per = sb::mmPerUnit(unit);
        const std::string size =
            faces ? mm(e.size / (per * per)) + " " + symbol + "^2" : length(e.size, unit) + " " + symbol;
        std::printf("  %2zu  %-8s %14s  at %s,%s,%s  %s\n", i + 1, geomWord(e.geom), size.c_str(),
                    length(e.center.x, unit).c_str(), length(e.center.y, unit).c_str(),
                    length(e.center.z, unit).c_str(),
                    sb::model::describeElement(doc, kind, names.of(kind)[k]).c_str());
    }
}

// Opens a design, rebuilds it and prints one line per step. Returns 1 if the
// file can't be opened or any active step fails.
int openDesign(int argc, char** argv) {
    const bool exporting = argc >= 5 && std::string_view(argv[3]) == "--export";
    const bool oneStep = argc == 7 && exporting && std::string_view(argv[5]) == "--step";
    const bool showSketch = argc == 5 && std::string_view(argv[3]) == "--sketch";
    const bool showEdges = argc == 5 && std::string_view(argv[3]) == "--edges";
    const bool showFaces = argc == 5 && std::string_view(argv[3]) == "--faces";
    if (argc != 3 && !(argc == 5 && exporting) && !oneStep && !showSketch && !showEdges && !showFaces) {
        return usage();
    }
    auto opened = sb::io::openFile(argv[2]);
    if (!opened.ok()) {
        printIssues(opened.issues);
        return 1;
    }
    const auto& doc = *opened.document;
    auto kernel = sb::kernel::makeOcctKernel();
    auto solver = sb::sketch::makePlaneGcsSolver();
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
        if (r->sketch && r->status == sb::model::FeatureStatus::Ok) {
            std::printf("  %s",
                        sb::cli::sketchSummary(static_cast<const sb::model::SketchFeature&>(f).sketch(),
                                               r->sketch->freedom)
                            .c_str());
        } else if (r->status == sb::model::FeatureStatus::Ok) {
            const auto& material = doc.materialOf(f.id());
            const auto mass = sb::model::bodyMass(*kernel, r->shape, material);
            std::printf("  volume %s", sb::volumeText(mass.volume, doc.lengthUnit()).c_str());
            // Finished bodies also get their mass and material.
            const auto& bodies = report.bodies();
            if (std::find(bodies.begin(), bodies.end(), f.id()) != bodies.end()) {
                std::printf("  mass %.2f g (%s)", mass.grams, material.name.c_str());
            }
        }
        std::printf("\n");
        sb::cli::printFormulas(f, doc.lengthUnit(), folder ? "      " : "    ");
        printIssues(r->issues);
    }
    sb::cli::printParameters(doc);
    if (showSketch) {
        const sb::model::Feature* found = nullptr;
        for (std::size_t i = 0; i < doc.featureCount(); ++i) {
            if (doc.featureAt(i).name() == argv[4]) found = &doc.featureAt(i);
        }
        if (!found || found->type() != sb::model::FeatureType::Sketch) {
            std::fprintf(stderr, "error: this design has no sketch called %s.\n", argv[4]);
            return 1;
        }
        printSketch(static_cast<const sb::model::SketchFeature&>(*found), *report.result(found->id()),
                    doc.lengthUnit());
    }
    if (showEdges || showFaces) {
        const sb::model::Feature* found = nullptr;
        for (std::size_t i = 0; i < doc.featureCount(); ++i) {
            if (doc.featureAt(i).name() == argv[4]) found = &doc.featureAt(i);
        }
        const auto* r = found ? report.result(found->id()) : nullptr;
        if (!r || !r->names) {
            std::fprintf(stderr,
                         found ? "error: %s has no body to list (it's a sketch, or has a problem).\n"
                               : "error: this design has no step called %s.\n",
                         argv[4]);
            return 1;
        }
        printElements(doc, *found, *r->names,
                      showEdges ? sb::kernel::TopoKind::Edge : sb::kernel::TopoKind::Face);
    }
    if (!report.ok()) return 1;

    if (exporting) {
        const std::filesystem::path out = argv[4];
        const sb::kernel::ShapeHandle* shape = nullptr;
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

// Lists SketchBillet's materials, and a design's own ones after them.
int materials(int argc, char** argv) {
    if (argc > 3) return usage();
    std::vector<sb::model::Material> list = sb::model::builtinMaterials();
    const std::size_t builtinCount = list.size();
    std::unique_ptr<sb::model::Document> doc;
    if (argc == 3) {
        auto opened = sb::io::openFile(argv[2]);
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
    using namespace sb::kernel;
    if (out.extension() == ".sb") return demoSketchBillet(out);
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
    sb::cli::RegressionOptions options;
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
    auto kernel = sb::kernel::makeOcctKernel();
    return sb::cli::runRegression(*kernel, paths, options);
}

}  // namespace

int main(int argc, char** argv) {
    // No buffering, so normal output and error messages come out in the order
    // they happen (the CLI reference's examples are checked against it).
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) return usage();
    const std::string_view cmd = argv[1];
    if (cmd == "--version") {
        std::printf("sketchbillet_cli %.*s\n", static_cast<int>(sb::version().size()), sb::version().data());
        return 0;
    }
    if (cmd == "demo" && argc == 3) return demo(argv[2]);
    if (cmd == "open") return openDesign(argc, argv);
    if (cmd == "materials") return materials(argc, argv);
    if (cmd == "regress") return regress(argc, argv);
    if (cmd == "plugins") return sb::cli::listPlugins(argc, argv);
    if (cmd == "help") return sb::cli::help(argc, argv);
    if (cmd == "run") return sb::cli::runCommand(argc, argv);
    return usage();
}
