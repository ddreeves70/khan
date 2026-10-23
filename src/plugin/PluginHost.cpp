#include "kh/plugin/PluginHost.h"

#include <algorithm>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

#include "Library.h"

namespace kh::plugin {

namespace {

constexpr std::string_view kReservedPrefix = "kh.";

Diagnostic problem(std::string code, std::string message, std::string detail = {}) {
    return {std::move(code), Severity::Error, std::move(message), std::move(detail)};
}

std::string displayName(const PluginManifest& m) {
    if (!m.name.empty()) return "The " + m.name + " plugin";
    if (!m.id.empty()) return "The plugin " + m.id;
    return "The plugin in " + m.folder.string();
}

bool startsWith(std::string_view s, std::string_view prefix) {
    return s.substr(0, prefix.size()) == prefix;
}

// The context given to one plugin. Commands are collected here while the
// register function runs, and handed to the host only if it succeeds.
class HostContext final : public PluginContext {
public:
    HostContext(PluginInfo& info, model::CommandRegistry& all, EventBus& bus, DocumentList& docs)
        : info_(&info), all_(all), bus_(bus), docs_(docs) {}

    const PluginManifest& manifest() const override { return info_->manifest; }

    void addCommand(model::CommandInfo info, model::CommandFactory factory) override {
        if (!registering_) {
            log("Ignored command " + info.name + ": commands can only be added while the plugin starts.");
            return;
        }
        staged_.add(std::move(info), std::move(factory));
    }

    const model::CommandRegistry& commands() const override { return all_; }
    EventBus& events() override { return bus_; }
    DocumentList& documents() override { return docs_; }
    void log(std::string message) override { info_->messages.push_back(std::move(message)); }
    void fail(std::string message) override { failures_.push_back(std::move(message)); }
    void keep(Subscription subscription) override { subscriptions_.push_back(std::move(subscription)); }

    // Called by the host after the register function returns.
    bool registering_ = true;
    model::CommandRegistry staged_;
    std::vector<std::string> failures_;
    std::vector<Subscription> subscriptions_;

    // The host moves each PluginInfo to its final place after loading.
    void retarget(PluginInfo& info) { info_ = &info; }

private:
    PluginInfo* info_;
    model::CommandRegistry& all_;
    EventBus& bus_;
    DocumentList& docs_;
};

}  // namespace

// One plugin found while searching, before it is loaded.
struct PluginHost::Candidate {
    RegisterFn registerFn = nullptr;  // built-ins only
    bool usable = true;               // false if its manifest or checks already failed
    HostContext* context = nullptr;   // set once its register function has run
};

std::string libraryFileName(std::string_view library) {
#if defined(_WIN32)
    return std::string(library) + ".dll";
#elif defined(__APPLE__)
    return std::string(library) + ".dylib";
#else
    return std::string(library) + ".so";
#endif
}

PluginHost::PluginHost(kernel::IKernel* kernel) : documents_(bus_, registry_, kernel) {
    bus_.setErrorHandler([this](const Diagnostic& d) { eventProblems_.push_back(d); });
}

PluginHost::~PluginHost() {
    // Members go in reverse order of declaration (see the header): contexts and
    // their listeners, then designs, the registry and the bus, and the
    // libraries last. Silence the bus first so nothing reaches a dying host.
    bus_.setErrorHandler({});
}

void PluginHost::addBuiltin(BuiltinPlugin plugin) {
    builtins_.push_back(std::move(plugin));
}

void PluginHost::addFolder(std::filesystem::path folder) {
    folders_.push_back(std::move(folder));
}

const PluginInfo* PluginHost::find(std::string_view id) const {
    for (const auto& p : infos_) {
        if (p.manifest.id == id) return &p;
    }
    return nullptr;
}

void PluginHost::loadAll() {
    if (loaded_) return;
    loaded_ = true;

    // 1. Find every plugin: built-ins first, then each folder's subfolders by name.
    std::vector<PluginInfo> found;
    std::vector<Candidate> candidates;
    auto addFound = [&](ManifestResult r, bool builtin, const std::filesystem::path& folder, RegisterFn fn) {
        PluginInfo info;
        info.builtin = builtin;
        Candidate c;
        c.registerFn = fn;
        if (r.ok()) {
            info.manifest = std::move(*r.manifest);
        } else {
            info.manifest.folder = folder;
            info.manifest.name = folder.filename().string();
            info.issues = std::move(r.issues);
            c.usable = false;
        }
        found.push_back(std::move(info));
        candidates.push_back(c);
    };
    for (auto& b : builtins_) addFound(parseManifest(b.manifestJson), true, {}, b.registerFn);
    for (const auto& folder : folders_) {
        std::error_code ec;
        if (!std::filesystem::is_directory(folder, ec)) continue;
        std::vector<std::filesystem::path> subfolders;
        for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
            if (entry.is_directory(ec) && std::filesystem::exists(entry.path() / "plugin.json", ec)) {
                subfolders.push_back(entry.path());
            }
        }
        std::sort(subfolders.begin(), subfolders.end());
        for (const auto& sub : subfolders) addFound(readManifest(sub), false, sub, nullptr);
    }

    // 2. Check each one on its own.
    std::map<std::string, std::size_t, std::less<>> byId;  // first plugin with each id
    for (std::size_t i = 0; i < found.size(); ++i) {
        auto& info = found[i];
        auto& c = candidates[i];
        if (!c.usable) continue;
        const auto& m = info.manifest;
        const auto who = displayName(m);
        auto refuse = [&](Diagnostic d) {
            info.issues.push_back(std::move(d));
            c.usable = false;
        };
        if (byId.count(m.id)) {
            refuse(problem("plugin.duplicate",
                           who + " has the same id (" + m.id +
                               ") as another plugin, so only the first one loads. Remove one of them.",
                           m.folder.string()));
            continue;
        }
        byId.emplace(m.id, i);
        if (m.apiVersion != kPluginApiVersion) {
            refuse(problem("plugin.api_version",
                           who + " was made for a different version of Khan (plugin API " +
                               std::to_string(m.apiVersion) + "; this Khan uses " +
                               std::to_string(kPluginApiVersion) +
                               "). Get a version of the plugin made for this Khan.",
                           m.folder.string()));
        } else if (!info.builtin && startsWith(m.id, kReservedPrefix)) {
            refuse(problem("plugin.reserved_id",
                           who +
                               " uses an id starting with \"kh.\", which is kept for Khan's own plugins. "
                               "Its author should pick another id, like \"yourname." +
                               m.id.substr(kReservedPrefix.size()) + "\".",
                           m.folder.string()));
        } else if (!info.builtin && m.library.empty()) {
            refuse(problem("plugin.no_library",
                           who + " has no \"library\" in its plugin.json. Plugins written in Python "
                                 "come in a later version of Khan.",
                           m.folder.string()));
        }
    }

    // 3. Load in dependency order. Each pass loads every plugin whose needs are
    //    settled, in id order, so the order is the same on every computer.
    std::vector<std::size_t> pending;
    for (const auto& [id, i] : byId) pending.push_back(i);  // sorted by id
    std::vector<std::size_t> loadOrder;
    std::set<std::size_t> settled;
    bool progress = true;
    while (progress && !pending.empty()) {
        progress = false;
        for (auto it = pending.begin(); it != pending.end();) {
            const auto i = *it;
            auto& info = found[i];
            bool ready = true;
            for (const auto& dep : info.manifest.dependencies) {
                auto d = byId.find(dep);
                if (d != byId.end() && !settled.count(d->second)) ready = false;
            }
            if (!ready) {
                ++it;
                continue;
            }
            if (candidates[i].usable) {
                for (const auto& dep : info.manifest.dependencies) {
                    auto d = byId.find(dep);
                    if (d == byId.end()) {
                        info.issues.push_back(problem("plugin.missing_dependency",
                                                      displayName(info.manifest) + " needs the plugin " +
                                                          dep + ", which isn't installed. Install " + dep +
                                                          " and restart Khan."));
                        candidates[i].usable = false;
                        break;
                    }
                    const auto& needed = found[d->second];
                    if (needed.state != PluginState::Loaded) {
                        info.state = PluginState::Skipped;
                        info.issues.push_back(
                            problem("plugin.dependency_failed",
                                    displayName(info.manifest) + " needs " +
                                        (needed.manifest.name.empty() ? dep : needed.manifest.name) +
                                        ", which didn't load. Fix that plugin first."));
                        candidates[i].usable = false;
                        break;
                    }
                }
            }
            if (candidates[i].usable) loadOne(candidates[i], info);
            settled.insert(i);
            loadOrder.push_back(i);
            it = pending.erase(it);
            progress = true;
        }
    }
    for (auto i : pending) {  // still waiting on each other
        found[i].issues.push_back(problem(
            "plugin.cycle", displayName(found[i].manifest) +
                                " and another plugin each need the other to load first, so neither "
                                "can. Their authors need to remove one of the \"requires\" entries."));
        found[i].state = PluginState::Failed;
        loadOrder.push_back(i);
    }

    // 4. List them: loaded ones in load order, then the rest.
    std::stable_partition(loadOrder.begin(), loadOrder.end(),
                          [&](std::size_t i) { return found[i].state == PluginState::Loaded; });
    for (std::size_t i = 0; i < found.size(); ++i) {
        if (std::find(loadOrder.begin(), loadOrder.end(), i) == loadOrder.end()) loadOrder.push_back(i);
    }
    // Each context points at its PluginInfo: move the infos to their final place
    // (reserved, so they never move again) and point each context there.
    infos_.reserve(found.size());
    for (auto i : loadOrder) {
        infos_.push_back(std::move(found[i]));
        if (candidates[i].context) candidates[i].context->retarget(infos_.back());
    }
}

void PluginHost::loadOne(Candidate& c, PluginInfo& info) {
    const auto& m = info.manifest;
    const auto who = displayName(m);
    auto failed = [&](Diagnostic d) {
        info.state = PluginState::Failed;
        info.issues.push_back(std::move(d));
    };

    RegisterFn fn = c.registerFn;
    if (!info.builtin) {
        const auto file = m.folder / libraryFileName(m.library);
        std::error_code ec;
        if (!std::filesystem::exists(file, ec)) {
            failed(problem("plugin.library_missing",
                           who + " is missing its program file, " + file.filename().string() +
                               ". Reinstall the plugin, or get a version made for this computer.",
                           file.string()));
            return;
        }
        std::string why;
        auto lib = detail::Library::open(file, why);
        if (!lib) {
            failed(problem("plugin.library_bad",
                           who + "'s program file couldn't be loaded. It may be made for another computer or "
                                 "another version of Khan. Get a version made for this Khan.",
                           file.string() + ": " + why));
            return;
        }
        using VersionFn = int (*)();
        auto version = lib->function<VersionFn>("kh_plugin_api_version");
        fn = lib->function<RegisterFn>("kh_plugin_register");
        if (!version || !fn) {
            failed(problem("plugin.library_bad",
                           who + "'s program file isn't a Khan plugin (it has no KH_PLUGIN_ENTRY). "
                                 "Ask its author for a fixed version.",
                           file.string()));
            libraries_.push_back(std::move(lib));
            return;
        }
        if (const int v = version(); v != kPluginApiVersion) {
            failed(problem("plugin.api_version",
                           who + " was built for a different version of Khan (plugin API " +
                               std::to_string(v) + "; this Khan uses " + std::to_string(kPluginApiVersion) +
                               "). Get a version of the plugin made for this Khan.",
                           file.string()));
            libraries_.push_back(std::move(lib));
            return;
        }
        // Kept until the host is destroyed, even if registering fails: the
        // plugin may have handed out code (listeners) that must stay valid.
        libraries_.push_back(std::move(lib));
    }

    auto ctx = std::make_unique<HostContext>(info, registry_, bus_, documents_);
    auto& hc = *ctx;
    c.context = &hc;
    try {
        fn(hc);
    } catch (const std::exception& e) {
        hc.failures_.push_back(std::string("It stopped with an error (") + e.what() +
                               "). Ask its author for help.");
    } catch (...) {
        hc.failures_.push_back("It stopped with an unknown error. Ask its author for help.");
    }
    hc.registering_ = false;

    // Check its commands before adding any of them.
    std::vector<std::string> names;
    for (const auto* cmd : hc.staged_.commands()) {
        if (!info.builtin && !startsWith(cmd->name, m.id + ".")) {
            hc.failures_.push_back("Its command " + cmd->name + " must start with \"" + m.id +
                                   ".\". Ask its author for a fixed version.");
        } else if (registry_.info(cmd->name)) {
            hc.failures_.push_back("Its command " + cmd->name +
                                   " has the same name as a command another plugin already added. "
                                   "Turn off one of the two plugins.");
        }
        names.push_back(cmd->name);
    }

    if (!hc.failures_.empty()) {
        for (auto& f : hc.failures_) {
            // f is a whole sentence: from fail() it is the plugin's own words.
            failed(problem("plugin.register_failed", who + " couldn't start. " + f, m.id));
        }
        hc.subscriptions_.clear();  // disconnect its listeners now
        contexts_.push_back(std::move(ctx));
        return;
    }

    for (const auto& name : names) {
        registry_.add(*hc.staged_.info(name), *hc.staged_.factory(name));
    }
    info.commands = std::move(names);
    info.state = PluginState::Loaded;
    contexts_.push_back(std::move(ctx));
}

}  // namespace kh::plugin
