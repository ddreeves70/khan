#include "Plugins.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
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
#include "kh/sketch_planegcs/PlaneGcsSolver.h"

namespace kh::cli {

namespace {

void printIssues(const Diagnostics& issues, const char* indent = "") {
    for (const auto& d : issues) {
        const char* kind = d.severity == Severity::Error     ? "error"
                           : d.severity == Severity::Warning ? "warning"
                                                             : "note";
        std::fprintf(stderr, "%s%s [%s]: %s", indent, kind, d.code.c_str(), d.message.c_str());
        if (!d.detail.empty()) std::fprintf(stderr, " (%s)", d.detail.c_str());
        std::fprintf(stderr, "\n");
    }
}

// Same words as kh_cli open.
const char* statusName(model::FeatureStatus s) {
    switch (s) {
        case model::FeatureStatus::Ok:
            return "ok";
        case model::FeatureStatus::Failed:
            return "failed";
        case model::FeatureStatus::Blocked:
            return "blocked";
        case model::FeatureStatus::Suppressed:
            return "off";
    }
    return "?";
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

// Takes the "--plugins <folder>" and "--slow-start-ms <n>" options off the
// argument list, then loads the plugins. False (with a message) if an option is bad.
bool loadPlugins(plugin::PluginHost& host, std::vector<std::string_view>& args) {
    host.addBuiltin(plugins::modelingPlugin());
    for (std::size_t i = 0; i + 1 < args.size();) {
        if (args[i] == "--plugins") {
            host.addFolder(std::filesystem::path(std::string(args[i + 1])));
        } else if (args[i] == "--slow-start-ms") {
            const std::string text(args[i + 1]);
            char* end = nullptr;
            const long ms = std::strtol(text.c_str(), &end, 10);
            if (text.empty() || *end != '\0' || ms < 0) {
                std::fprintf(stderr,
                             "error: --slow-start-ms needs a whole number of milliseconds, like 500 (0 turns "
                             "the warning off).\n");
                return false;
            }
            host.setSlowStartLimit(std::chrono::milliseconds(ms));
        } else {
            ++i;
            continue;
        }
        args.erase(args.begin() + static_cast<std::ptrdiff_t>(i),
                   args.begin() + static_cast<std::ptrdiff_t>(i) + 2);
    }
    host.loadAll();
    return true;
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
        case ArgType::Vec2: {
            const auto c = text.find(',');
            if (c != std::string::npos) {
                auto x = toNumber(text.substr(0, c)), y = toNumber(text.substr(c + 1));
                if (x && y) return sketch::Point2{*x, *y};
            }
            why = spec.name + " must be two numbers in mm, like 10,20";
            return std::nullopt;
        }
        case ArgType::Vec2List: {
            // Points split by "/", so the shell needs no quotes: 0,0/0,30/40,30.
            std::vector<sketch::Point2> points;
            std::size_t from = 0;
            for (;;) {
                const auto slash = text.find('/', from);
                const std::string item =
                    text.substr(from, slash == std::string::npos ? std::string::npos : slash - from);
                const auto c = item.find(',');
                auto x = c == std::string::npos ? std::nullopt : toNumber(item.substr(0, c));
                auto y = c == std::string::npos ? std::nullopt : toNumber(item.substr(c + 1));
                if (!x || !y) {
                    why = spec.name + " must be points in mm split by /, like 0,0/0,30/40,30/40,0";
                    return std::nullopt;
                }
                points.push_back({*x, *y});
                if (slash == std::string::npos) return points;
                from = slash + 1;
            }
        }
        case ArgType::FeatureRef:
            for (std::size_t i = 0; i < doc.featureCount(); ++i) {
                if (doc.featureAt(i).name() == text) return doc.featureAt(i).id();
            }
            if (auto id = FeatureId::parse(text)) return *id;
            why = spec.name + " must be the name of a step in the design, like Box1";
            return std::nullopt;
        case ArgType::FolderRef:
            if (const auto* folder = doc.findFolder(std::string_view(text))) return folder->id;
            if (auto id = FolderId::parse(text)) return *id;
            why = spec.name +
                  " must be the name of a folder in the design, like Folder1 (there's no folder called " +
                  text + ")";
            return std::nullopt;
        case ArgType::FeatureList: {
            auto byName = [&](const std::string& name) -> std::optional<FeatureId> {
                for (std::size_t i = 0; i < doc.featureCount(); ++i) {
                    if (doc.featureAt(i).name() == name) return doc.featureAt(i).id();
                }
                return FeatureId::parse(name);
            };
            if (auto one = byName(text)) return std::vector<FeatureId>{*one};  // a name with a comma in it
            std::vector<FeatureId> ids;
            std::size_t from = 0;
            for (;;) {
                const auto comma = text.find(',', from);
                const std::string name =
                    text.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
                auto id = byName(name);
                if (!id) {
                    why = spec.name +
                          " must be names of steps in the design, separated by commas, like Box1,Hole" +
                          (name.empty() ? std::string() : " (there's no step called " + name + ")");
                    return std::nullopt;
                }
                ids.push_back(*id);
                if (comma == std::string::npos) return ids;
                from = comma + 1;
            }
        }
    }
    return std::nullopt;
}

}  // namespace

namespace {

const char* typeName(model::ArgType t) {
    switch (t) {
        case model::ArgType::Number:
            return "number";
        case model::ArgType::Integer:
            return "whole number";
        case model::ArgType::Bool:
            return "true or false";
        case model::ArgType::Text:
            return "text";
        case model::ArgType::Vec3:
            return "x,y,z";
        case model::ArgType::Vec2:
            return "x,y";
        case model::ArgType::Vec2List:
            return "x,y/x,y/...";
        case model::ArgType::FeatureRef:
            return "step name";
        case model::ArgType::FeatureList:
            return "step names";
        case model::ArgType::FolderRef:
            return "folder name";
        case model::ArgType::Choice:
            return "choice";
    }
    return "?";
}

// Which plugin added a command.
std::string ownerOf(const plugin::PluginHost& host, const std::string& name) {
    for (const auto& p : host.plugins()) {
        for (const auto& c : p.commands) {
            if (c == name) return p.manifest.id;
        }
    }
    return "?";
}

}  // namespace

int help(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 2, argv + argc);
    plugin::PluginHost host;
    if (!loadPlugins(host, args)) return 2;
    if (args.size() > 1) {
        std::fprintf(stderr, "usage: kh_cli help [--plugins <folder>]... [<command>]\n");
        return 2;
    }
    if (args.empty()) {
        std::printf("Commands for kh_cli run:\n");
        for (const auto* c : host.commands().commands()) {
            std::printf("  %-24s %s\n", c->name.c_str(), c->label.c_str());
        }
        std::printf("Run \"kh_cli help <command>\" to see a command's settings.\n");
        return 0;
    }
    const std::string name(args[0]);
    const auto* c = host.commands().info(name);
    if (!c) {
        std::fprintf(stderr, "error: there is no command called %s. Run \"kh_cli help\" to see them.\n",
                     name.c_str());
        return 1;
    }
    std::printf("%s: %s (from %s)\n", c->name.c_str(), c->label.c_str(), ownerOf(host, name).c_str());
    std::printf("  %s\n", c->description.c_str());
    if (c->args.empty()) {
        std::printf("  No settings.\n");
        return 0;
    }
    std::printf("Settings:\n");
    for (const auto& a : c->args) {
        std::string kind = typeName(a.type);
        if (a.type == model::ArgType::Choice) {
            kind.clear();
            for (const auto& choice : a.choices) kind += (kind.empty() ? "" : "|") + choice;
        }
        std::printf("  %-10s %-20s %-9s %s\n", a.name.c_str(), kind.c_str(),
                    a.required ? "required" : "optional", a.description.c_str());
    }
    return 0;
}

int listPlugins(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 2, argv + argc);
    const auto strict = std::find(args.begin(), args.end(), "--strict");
    const bool isStrict = strict != args.end();
    if (isStrict) args.erase(strict);
    plugin::PluginHost host;
    if (!loadPlugins(host, args)) return 2;
    if (!args.empty()) {
        std::fprintf(stderr,
                     "usage: kh_cli plugins [--plugins <folder>]... [--slow-start-ms <n>] [--strict]\n");
        return 2;
    }

    bool allLoaded = true;
    bool warned = false;
    for (const auto& p : host.plugins()) {
        const auto& m = p.manifest;
        std::printf("%-8s %s %s  %s%s\n", stateName(p.state), m.id.empty() ? "(no id)" : m.id.c_str(),
                    m.version.c_str(), m.name.c_str(), p.builtin ? " (built in)" : "");
        if (!m.tier.empty()) std::printf("           tier    %s\n", m.tier.c_str());
        for (const auto& t : m.tiers) {
            std::string where;
            if (!t.after.empty()) where += "after " + t.after;
            if (!t.before.empty()) where += (where.empty() ? "" : ", ") + std::string("before ") + t.before;
            std::printf("           adds tier %s (%s)\n", t.name.c_str(), where.c_str());
        }
        for (const auto& c : p.commands) std::printf("           command %s\n", c.c_str());
        for (const auto& svc : p.services) std::printf("           service %s\n", svc.c_str());
        for (const auto& a : host.actions()) {
            if (a.plugin != m.id) continue;
            // An action runs a command, or the plugin's own code (often with a check mark).
            const std::string what = !a.command.empty() ? a.command
                                     : a.checked        ? std::string("check mark")
                                                        : std::string("runs plugin code");
            std::printf("           action  %s > %s  (%s)\n", a.menu.c_str(), a.text.c_str(), what.c_str());
        }
        for (const auto& line : p.messages) std::printf("           log: %s\n", line.c_str());
        printIssues(p.issues, "           ");
        if (p.state != plugin::PluginState::Loaded) allLoaded = false;
        for (const auto& d : p.issues) warned = warned || d.severity == Severity::Warning;
    }
    // --strict: warnings fail too, for plugin authors' CI.
    return allLoaded && !(isStrict && warned) ? 0 : 1;
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
    auto solver = sketch::makePlaneGcsSolver();
    plugin::PluginHost host(kernel.get(), solver.get());
    if (!loadPlugins(host, args)) return 2;
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
        // A folder's name comes before its first step, and its steps are indented.
        const auto* folder = design.findFolder(design.folderOf(f.id()));
        if (folder && folder->steps.front() == f.id()) std::printf("  %s/\n", folder->name.c_str());
        std::printf(folder ? "    %-10s %s" : "  %-12s %s", f.name().c_str(), statusName(r->status));
        if (r->sketch && r->status == model::FeatureStatus::Ok) {
            std::printf("  %s", sketchSummary(static_cast<const model::SketchFeature&>(f).sketch(),
                                              r->sketch->freedom)
                                    .c_str());
        } else if (r->status == model::FeatureStatus::Ok) {
            const auto& material = design.materialOf(f.id());
            const auto mass = model::bodyMass(*kernel, r->shape, material);
            std::printf("  volume %s", volumeText(mass.volume, design.lengthUnit()).c_str());
            // Finished bodies also get their mass and material.
            const auto& bodies = report->bodies();
            if (std::find(bodies.begin(), bodies.end(), f.id()) != bodies.end()) {
                std::printf("  mass %.2f g (%s)", mass.grams, material.name.c_str());
            }
        }
        std::printf("\n");
        printIssues(r->issues, "    ");
    }
    printParameters(design);
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

void printParameters(const model::Document& doc) {
    const auto& table = doc.parameters();
    if (table.empty()) return;
    const auto values = table.evaluate();
    std::size_t width = 0;
    for (const auto& p : table.parameters()) width = std::max(width, p.name.size());
    std::printf("Parameters:\n");
    for (const auto& p : table.parameters()) {
        std::printf("  %-*s = %s", static_cast<int>(width), p.name.c_str(), p.expression.c_str());
        const auto* v = values.find(p.name);
        if (v && v->ok()) {
            const auto text = model::quantityText(*v->value, doc.lengthUnit());
            if (text != p.expression) std::printf("  (%s)", text.c_str());
            std::printf("\n");
        } else {
            std::printf("\n");
            for (const auto& d : v ? v->issues : Diagnostics{}) {
                std::printf("    problem [%s]: %s\n", d.code.c_str(), d.message.c_str());
            }
        }
    }
}

std::string sketchSummary(const sketch::Sketch& sketch, int freedom) {
    int points = 0, lines = 0, circles = 0, arcs = 0, splines = 0;
    for (const auto& e : sketch.entities) {
        points += std::holds_alternative<sketch::PointShape>(e.shape) ? 1 : 0;
        lines += std::holds_alternative<sketch::LineShape>(e.shape) ? 1 : 0;
        circles += std::holds_alternative<sketch::CircleShape>(e.shape) ? 1 : 0;
        arcs += std::holds_alternative<sketch::ArcShape>(e.shape) ? 1 : 0;
        splines += std::holds_alternative<sketch::SplineShape>(e.shape) ? 1 : 0;
    }
    auto count = [](int n, const char* one, const char* many) {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    };
    std::string out;
    auto add = [&](const std::string& part) { out += (out.empty() ? "" : ", ") + part; };
    if (lines) add(count(lines, "line", "lines"));
    if (circles) add(count(circles, "circle", "circles"));
    if (arcs) add(count(arcs, "arc", "arcs"));
    if (splines) add(count(splines, "spline", "splines"));
    if (points) add(count(points, "point", "points"));
    if (out.empty()) return "empty";
    add(count(static_cast<int>(sketch.constraints.size()), "constraint", "constraints"));
    add(freedom == 0 ? std::string("fully constrained") : "free to move " + count(freedom, "way", "ways"));
    return out;
}

}  // namespace kh::cli
