#include "Plugins.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kh/io/KhanFile.h"
#include "kh/kernel_occt/OcctKernel.h"
#include "kh/plugin/PluginHost.h"
#include "kh/plugins/Modeling.h"

namespace kh::cli {

namespace {

void printIssues(const Diagnostics& issues, const char* indent = "") {
    for (const auto& d : issues) {
        std::fprintf(stderr, "%serror [%s]: %s", indent, d.code.c_str(), d.message.c_str());
        if (!d.detail.empty()) std::fprintf(stderr, " (%s)", d.detail.c_str());
        std::fprintf(stderr, "\n");
    }
}

const char* stateName(plugin::PluginState s) {
    switch (s) {
        case plugin::PluginState::Loaded:
            return "loaded";
        case plugin::PluginState::Failed:
            return "failed";
        case plugin::PluginState::Skipped:
            return "skipped";
    }
    return "?";
}

// Takes the leading "--plugins <folder>" options off the argument list.
void loadPlugins(plugin::PluginHost& host, std::vector<std::string_view>& args) {
    host.addBuiltin(plugins::modelingPlugin());
    for (std::size_t i = 0; i + 1 < args.size();) {
        if (args[i] == "--plugins") {
            host.addFolder(std::filesystem::path(std::string(args[i + 1])));
            args.erase(args.begin() + static_cast<std::ptrdiff_t>(i),
                       args.begin() + static_cast<std::ptrdiff_t>(i) + 2);
        } else {
            ++i;
        }
    }
    host.loadAll();
}

std::optional<std::string> takeOption(std::vector<std::string_view>& args, std::string_view name) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == name) {
            std::string value(args[i + 1]);
            args.erase(args.begin() + static_cast<std::ptrdiff_t>(i),
                       args.begin() + static_cast<std::ptrdiff_t>(i) + 2);
            return value;
        }
    }
    return std::nullopt;
}

std::optional<double> toNumber(const std::string& s) {
    errno = 0;
    char* end = nullptr;
    const double d = std::strtod(s.c_str(), &end);
    if (s.empty() || errno != 0 || *end != '\0') return std::nullopt;
    return d;
}

// Turns "name=value" text into a typed argument, using the command's description.
std::optional<model::ArgValue> parseArg(const model::ArgSpec& spec, const std::string& text,
                                        const model::Document& doc, std::string& why) {
    using model::ArgType;
    switch (spec.type) {
        case ArgType::Number:
            if (auto d = toNumber(text)) return *d;
            why = spec.name + " must be a number, like 20 or 2.5";
            return std::nullopt;
        case ArgType::Integer: {
            auto d = toNumber(text);
            if (d && *d == static_cast<double>(static_cast<std::int64_t>(*d)))
                return static_cast<std::int64_t>(*d);
            why = spec.name + " must be a whole number, like 3";
            return std::nullopt;
        }
        case ArgType::Bool:
            if (text == "true" || text == "yes") return true;
            if (text == "false" || text == "no") return false;
            why = spec.name + " must be true or false";
            return std::nullopt;
        case ArgType::Text:
        case ArgType::Choice:
            return text;
        case ArgType::Vec3: {
            const auto c1 = text.find(',');
            const auto c2 = c1 == std::string::npos ? c1 : text.find(',', c1 + 1);
            if (c2 != std::string::npos) {
                auto x = toNumber(text.substr(0, c1)), y = toNumber(text.substr(c1 + 1, c2 - c1 - 1)),
                     z = toNumber(text.substr(c2 + 1));
                if (x && y && z) return kernel::Vec3{*x, *y, *z};
            }
            why = spec.name + " must be three numbers in mm, like 10,20,30";
            return std::nullopt;
        }
        case ArgType::FeatureRef:
            for (std::size_t i = 0; i < doc.featureCount(); ++i) {
                if (doc.featureAt(i).name() == text) return doc.featureAt(i).id();
            }
            if (auto id = FeatureId::parse(text)) return *id;
            why = spec.name + " must be the name of a step in the design, like Box1";
            return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace

int listPlugins(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 2, argv + argc);
    plugin::PluginHost host;
    loadPlugins(host, args);
    if (!args.empty()) {
        std::fprintf(stderr, "usage: kh_cli plugins [--plugins <folder>]...\n");
        return 2;
    }

    bool allLoaded = true;
    for (const auto& p : host.plugins()) {
        const auto& m = p.manifest;
        std::printf("%-8s %s %s  %s%s\n", stateName(p.state), m.id.empty() ? "(no id)" : m.id.c_str(),
                    m.version.c_str(), m.name.c_str(), p.builtin ? " (built in)" : "");
        for (const auto& c : p.commands) std::printf("           command %s\n", c.c_str());
        for (const auto& line : p.messages) std::printf("           log: %s\n", line.c_str());
        printIssues(p.issues, "           ");
        if (p.state != plugin::PluginState::Loaded) allLoaded = false;
    }
    return allLoaded ? 0 : 1;
}

int runCommand(int argc, char** argv) {
    auto usage = [] {
        std::fprintf(stderr,
                     "usage: kh_cli run [--plugins <folder>]... [--open <in.khan>] [--save <out.khan>] "
                     "<command> [name=value]...\n");
        return 2;
    };
    std::vector<std::string_view> args(argv + 2, argv + argc);
    auto kernel = kernel::makeOcctKernel();
    plugin::PluginHost host(kernel.get());
    loadPlugins(host, args);
    const auto openPath = takeOption(args, "--open");
    const auto savePath = takeOption(args, "--save");
    if (args.empty()) return usage();

    for (const auto& p : host.plugins()) {
        if (p.state != plugin::PluginState::Loaded) {
            std::fprintf(stderr, "warning: plugin %s didn't load (see kh_cli plugins)\n",
                         p.manifest.id.c_str());
        }
    }

    // The design: opened, or new.
    std::unique_ptr<model::Document> doc;
    if (openPath) {
        auto opened = io::openFile(*openPath);
        if (!opened.ok()) {
            printIssues(opened.issues);
            return 1;
        }
        doc = std::move(opened.document);
    } else {
        const auto name =
            savePath ? std::filesystem::path(*savePath).stem().string() : std::string("Untitled");
        doc = std::make_unique<model::Document>(model::DocKind::Part, name);
    }
    auto& docs = host.documents();
    const DocId id = docs.add(std::move(doc));

    // The command and its arguments.
    const std::string name(args[0]);
    const auto* info = host.commands().info(name);
    if (!info) {
        std::fprintf(stderr, "error: there is no command called %s. Run \"kh_cli plugins\" to see them.\n",
                     name.c_str());
        return 1;
    }
    model::Args values;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string arg(args[i]);
        const auto eq = arg.find('=');
        const auto argName = arg.substr(0, eq);
        const model::ArgSpec* spec = nullptr;
        for (const auto& s : info->args) {
            if (s.name == argName) spec = &s;
        }
        if (eq == std::string::npos || !spec) {
            std::fprintf(stderr, "error: %s has no setting called %s.", info->label.c_str(), argName.c_str());
            std::fprintf(stderr, " Its settings are:");
            for (const auto& s : info->args) std::fprintf(stderr, " %s", s.name.c_str());
            std::fprintf(stderr, "\n");
            return 1;
        }
        std::string why;
        auto value = parseArg(*spec, arg.substr(eq + 1), *docs.find(id), why);
        if (!value) {
            std::fprintf(stderr, "error: %s\n", why.c_str());
            return 1;
        }
        values[argName] = *value;
    }

    if (auto issues = docs.run(id, name, values); !issues.empty()) {
        printIssues(issues);
        return 1;
    }

    // Report every step, then what plugins logged.
    const auto& design = *docs.find(id);
    const auto* report = docs.lastRebuild(id);
    std::printf("%s: %zu %s\n", design.name().c_str(), design.featureCount(),
                design.featureCount() == 1 ? "step" : "steps");
    for (std::size_t i = 0; i < design.featureCount(); ++i) {
        const auto& f = design.featureAt(i);
        const auto* r = report->result(f.id());
        std::printf("  %-12s %s", f.name().c_str(), r->status == model::FeatureStatus::Ok ? "ok" : "problem");
        if (r->status == model::FeatureStatus::Ok) {
            std::printf("  volume %.2f mm^3", kernel->massProps(r->shape).volume);
        }
        std::printf("\n");
        printIssues(r->issues, "    ");
    }
    for (const auto& p : host.plugins()) {
        for (const auto& line : p.messages) std::printf("[%s] %s\n", p.manifest.id.c_str(), line.c_str());
    }

    if (savePath) {
        io::Bytes bytes;
        if (auto issues = io::writeBytes(design, bytes); !issues.empty()) {
            printIssues(issues);
            return 1;
        }
        std::ofstream out(*savePath, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            std::fprintf(stderr,
                         "error: Khan couldn't save %s. Check the folder exists and isn't read-only.\n",
                         savePath->c_str());
            return 1;
        }
        docs.markSaved(id);
        std::printf("wrote %s\n", savePath->c_str());
    }
    return report->ok() ? 0 : 1;
}

}  // namespace kh::cli
