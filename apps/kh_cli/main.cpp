// kh_cli: headless tool for scripting, smoke tests and the geometry regression corpus.
//
//   kh_cli --version
//   kh_cli demo <out.stl|out.step|out.khan> box with a hole, as a toolchain smoke test
//   kh_cli open <file.khan> [--export out [--step name]]  open a design, rebuild it, report each step
//   kh_cli regress [--update] <case|dir>... run regression cases (see tests/corpus/README.md)
//   kh_cli plugins [--plugins <folder>]...  list plugins, their commands and problems
//   kh_cli help [--plugins <folder>]... [<command>]  list commands, or describe one
//   kh_cli run [--plugins <folder>]... [--open in.khan] [--save out.khan] <command> [name=value]...

#include <cstdio>
#include <filesystem>
#include <memory>
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

namespace {

int usage() {
    std::puts(
        "usage: kh_cli --version\n"
        "       kh_cli demo <out.stl|out.step|out.khan>\n"
        "       kh_cli open <file.khan> [--export <out.stl|out.step> [--step <name>]]\n"
        "       kh_cli regress [--update] <case.json|folder>...\n"
        "       kh_cli plugins [--plugins <folder>]...\n"
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

// Opens a design, rebuilds it and prints one line per step. Returns 1 if the
// file can't be opened or any active step fails.
int openDesign(int argc, char** argv) {
    const bool exporting = argc >= 5 && std::string_view(argv[3]) == "--export";
    const bool oneStep = argc == 7 && exporting && std::string_view(argv[5]) == "--step";
    if (argc != 3 && !(argc == 5 && exporting) && !oneStep) return usage();
    auto opened = kh::io::openFile(argv[2]);
    if (!opened.ok()) {
        printIssues(opened.issues);
        return 1;
    }
    const auto& doc = *opened.document;
    auto kernel = kh::kernel::makeOcctKernel();
    const auto report = doc.regenerate(*kernel);

    std::printf("%s: %zu %s\n", doc.name().c_str(), doc.featureCount(),
                doc.featureCount() == 1 ? "step" : "steps");
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        const auto& f = doc.featureAt(i);
        const auto* r = report.result(f.id());
        std::printf("  %-12s %s", f.name().c_str(), statusName(r->status));
        if (r->status == kh::model::FeatureStatus::Ok) {
            std::printf("  volume %.2f mm^3", kernel->massProps(r->shape).volume);
        }
        std::printf("\n");
        printIssues(r->issues);
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
    if (cmd == "regress") return regress(argc, argv);
    if (cmd == "plugins") return kh::cli::listPlugins(argc, argv);
    if (cmd == "help") return kh::cli::help(argc, argv);
    if (cmd == "run") return kh::cli::runCommand(argc, argv);
    return usage();
}
