// kh_cli: headless tool for scripting, smoke tests and (from Phase 0 week 2)
// the geometry regression corpus.
//
//   kh_cli --version
//   kh_cli demo <out.stl|out.step>   box with a hole, as a toolchain smoke test

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

#include "kh/core/Version.h"
#include "kh/kernel_occt/OcctKernel.h"

namespace {

int usage() {
    std::puts("usage: kh_cli --version\n       kh_cli demo <out.stl|out.step>");
    return 2;
}

void printIssues(const kh::Diagnostics& issues) {
    for (const auto& d : issues) {
        std::fprintf(stderr, "error [%s]: %s %s\n", d.code.c_str(), d.message.c_str(), d.detail.c_str());
    }
}

int demo(const std::filesystem::path& out) {
    using namespace kh::kernel;
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

    const auto ext = out.extension().string();
    const auto format = (ext == ".step" || ext == ".stp") ? ExportFormat::Step : ExportFormat::Stl;
    auto issues = kernel->exportFile(tag.shape, format, out);
    if (!issues.empty()) {
        printIssues(issues);
        return 1;
    }
    std::printf("wrote %s\n", out.string().c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string_view cmd = argv[1];
    if (cmd == "--version") {
        std::printf("kh_cli %.*s\n", static_cast<int>(kh::version().size()), kh::version().data());
        return 0;
    }
    if (cmd == "demo" && argc == 3) return demo(argv[2]);
    return usage();
}
