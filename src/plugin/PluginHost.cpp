#include "kh/plugin/PluginHost.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <functional>
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

Diagnostic warning(std::string code, std::string message, std::string detail = {}) {
    return {std::move(code), Severity::Warning, std::move(message), std::move(detail)};
}

Diagnostic note(std::string code, std::string message, std::string detail = {}) {
    return {std::move(code), Severity::Info, std::move(message), std::move(detail)};
}

std::string displayName(const PluginManifest& m) {
    if (!m.name.empty()) return "The " + m.name + " plugin";
    if (!m.id.empty()) return "The plugin " + m.id;
    return "The plugin in " + m.folder.string();
}

// The plugin's name for the middle of a sentence: "Grid", or its id.
std::string shortName(const PluginManifest& m) {
    return m.name.empty() ? m.id : m.name;
}

bool startsWith(std::string_view s, std::string_view prefix) {
    return s.substr(0, prefix.size()) == prefix;
}

bool contains(const std::vector<std::string>& list, std::string_view s) {
    return std::find(list.begin(), list.end(), s) != list.end();
}

// "example.ruler/2" -> {"example.ruler", 2}; no version -> {name, -1}.
std::pair<std::string_view, int> splitVersion(std::string_view name) {
    const auto slash = name.rfind('/');
    if (slash == std::string_view::npos) return {name, -1};
    int v = 0;
    for (char c : name.substr(slash + 1)) {
        if (c < '0' || c > '9') return {name, -1};
        v = v * 10 + (c - '0');
    }
    return {name.substr(0, slash), v};
}

std::string milliseconds(std::chrono::steady_clock::duration d) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
    if (ms < 1000) return std::to_string(ms) + " ms";
    const auto tenths = ms / 100;
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " s";
}

}  // namespace

// What every plugin context shares with the host: the services, which phase
// loading is in, and every plugin's manifest (for checks).
struct detail::HostShared {
    struct Service {
        void* pointer = nullptr;
        std::string provider;  // plugin id; empty for the program running Khan
        bool deprecated = false;
    };
    enum class Phase { Register, Start, Running };

    std::map<std::string, Service, std::less<>> services;
    Phase phase = Phase::Register;
    std::map<std::string, const PluginManifest*, std::less<>> manifests;  // by id, while loading

    // The plugin whose id starts a name like "example.ruler.ready": the longest
    // plugin id followed by a dot. Empty if none.
    std::string ownerOf(std::string_view name) const {
        std::string best;
        for (const auto& [id, m] : manifests) {
            if (name.size() > id.size() && startsWith(name, id) && name[id.size()] == '.' &&
                id.size() > best.size())
                best = id;
        }
        return best;
    }
};

namespace {

using Shared = detail::HostShared;

// The context given to one plugin. Commands, actions and services are collected
// here while the register function runs, and handed to the host only if it succeeds.
class HostContext final : public PluginContext {
public:
    HostContext(PluginInfo& info, model::CommandRegistry& all, EventBus& bus, DocumentList& docs,
                Selection& selection, Shared& shared, SettingsBackend& settings, OverlayList& overlays)
        : info_(&info),
          all_(all),
          bus_(bus),
          docs_(docs),
          selection_(selection),
          shared_(shared),
          settings_(settings, bus, info.manifest.id),
          overlays_(overlays, info.manifest.id) {}

    const PluginManifest& manifest() const override { return info_->manifest; }

    void addCommand(model::CommandInfo info, model::CommandFactory factory) override {
        if (!registering_) {
            log("Ignored command " + info.name + ": commands can only be added while the plugin starts.");
            return;
        }
        staged_.add(std::move(info), std::move(factory));
    }

    void addAction(ActionSpec action) override {
        if (!registering_) {
            log("Ignored action " + action.id + ": actions can only be added while the plugin starts.");
            return;
        }
        action.plugin = info_->manifest.id;
        stagedActions_.push_back(std::move(action));
    }

    void provide(std::string name, void* service, bool deprecated) override {
        if (!registering_) {
            log("Ignored service " + name + ": services can only be offered while the plugin starts.");
            return;
        }
        stagedServices_.push_back({std::move(name), {service, info_->manifest.id, deprecated}});
    }

    void onStart(std::function<void()> start) override {
        if (!registering_) {
            log("Ignored an onStart callback: it can only be added while the plugin starts.");
            return;
        }
        starts_.push_back(std::move(start));
    }

    Selection& selection() override { return selection_; }
    PluginSettings& settings() override { return settings_; }
    PluginOverlays& overlays() override { return overlays_; }

    void* service(std::string_view name) override {
        auto it = shared_.services.find(name);
        if (it == shared_.services.end()) return nullptr;
        const auto& provider = it->second.provider;
        const auto& self = info_->manifest;
        if (!provider.empty() && provider != self.id) {
            const auto* by = providerManifest(provider);
            const std::string byName = by ? shortName(*by) : provider;
            if (shared_.phase == Shared::Phase::Register) {
                warnOnce(warning("plugin.early_lookup",
                                 shortName(self) + " asked for " + std::string(name) +
                                     " while registering. That only works because of the order plugins "
                                     "load in; look it up in onStart() instead."));
            }
            if (!contains(self.dependencies, provider) && !contains(self.uses, provider)) {
                warnOnce(warning("plugin.undeclared_dependency",
                                 shortName(self) + " uses " + std::string(name) + " from " + byName +
                                     " but doesn't list " + provider +
                                     " in its plugin.json. Add it to \"uses\" (or \"requires\") so "
                                     "Khan loads them in the right order."));
            }
            if (it->second.deprecated) {
                warnOnce(warning("plugin.old_contract",
                                 shortName(self) + " uses " + std::string(name) + ", which " + byName +
                                     " says is going away. Move to its newer version."));
            } else if (const auto [base, version] = splitVersion(name); version >= 0) {
                for (const auto& [other, entry] : shared_.services) {
                    const auto [otherBase, otherVersion] = splitVersion(other);
                    if (otherBase == base && otherVersion > version) {
                        warnOnce(warning("plugin.old_contract",
                                         shortName(self) + " uses " + std::string(name) + "; " + byName +
                                             " also offers " + other + ". " + std::string(name) +
                                             " may be removed in a later version."));
                        break;
                    }
                }
            }
        }
        return it->second.pointer;
    }

    void notePublish(std::string_view type) override {
        const auto& self = info_->manifest;
        if (!startsWith(type, self.id + ".")) {
            warnOnce(warning("plugin.event_name", shortName(self) + " sent the event " + std::string(type) +
                                                      ". Event names must start with the plugin id, like " +
                                                      self.id + ".something, so they don't clash."));
        }
    }

    void noteListen(std::string_view type) override {
        const auto& self = info_->manifest;
        const auto owner = shared_.ownerOf(type);
        if (owner.empty() || owner == self.id || contains(self.dependencies, owner) ||
            contains(self.uses, owner))
            return;
        const auto* by = providerManifest(owner);
        warnOnce(warning("plugin.undeclared_dependency",
                         shortName(self) + " listens to " + std::string(type) + " from " +
                             (by ? shortName(*by) : owner) + " but doesn't list " + owner +
                             " in its plugin.json. Add it to \"uses\" (or \"requires\") so Khan "
                             "loads them in the right order."));
    }

    const model::CommandRegistry& commands() const override { return all_; }
    EventBus& events() override { return bus_; }
    DocumentList& documents() override { return docs_; }
    void log(std::string message) override { info_->messages.push_back(std::move(message)); }
    void fail(std::string message) override { failures_.push_back(std::move(message)); }
    void keep(Subscription subscription) override { subscriptions_.push_back(std::move(subscription)); }

    // Adds a warning or note once per code and message.
    void warnOnce(Diagnostic d) {
        for (const auto& old : info_->issues) {
            if (old.code == d.code && old.message == d.message) return;
        }
        info_->issues.push_back(std::move(d));
    }

    // Called by the host after the register function returns.
    bool registering_ = true;
    model::CommandRegistry staged_;
    std::vector<ActionSpec> stagedActions_;
    std::vector<std::pair<std::string, Shared::Service>> stagedServices_;
    std::vector<std::function<void()>> starts_;
    std::vector<std::string> failures_;
    std::vector<Subscription> subscriptions_;
    std::chrono::steady_clock::duration took{};  // time spent registering and starting

    // The host moves each PluginInfo to its final place after loading.
    void retarget(PluginInfo& info) { info_ = &info; }

private:
    const PluginManifest* providerManifest(const std::string& id) const {
        auto it = shared_.manifests.find(id);
        return it == shared_.manifests.end() ? nullptr : it->second;
    }

    PluginInfo* info_;
    model::CommandRegistry& all_;
    EventBus& bus_;
    DocumentList& docs_;
    Selection& selection_;
    Shared& shared_;
    PluginSettings settings_;
    PluginOverlays overlays_;
};

// Runs a plugin's code, turning exceptions into failure messages.
template <typename F>
void guarded(HostContext& hc, F&& f) {
    try {
        f();
    } catch (const std::exception& e) {
        hc.failures_.push_back(std::string("It stopped with an error (") + e.what() +
                               "). Ask its author for help.");
    } catch (...) {
        hc.failures_.push_back("It stopped with an unknown error. Ask its author for help.");
    }
}

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

PluginHost::PluginHost(kernel::IKernel* kernel)
    : documents_(bus_, registry_, kernel),
      selection_(bus_, documents_),
      shared_(std::make_unique<detail::HostShared>()) {
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

void PluginHost::provide(std::string name, void* service) {
    shared_->services[std::move(name)] = {service, {}, false};
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
    for (auto& b : builtins_) addFound(parseBuiltinManifest(b.manifestJson), true, {}, b.registerFn);
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

    for (const auto& [id, i] : byId) shared_->manifests[id] = &found[i].manifest;
    // A plugin's dependencies that are installed: "requires" and "uses".
    auto depsOf = [&](std::size_t i) {
        std::vector<std::size_t> out;
        const auto& m = found[i].manifest;
        for (const auto* list : {&m.dependencies, &m.uses}) {
            for (const auto& dep : *list) {
                if (auto d = byId.find(dep); d != byId.end()) out.push_back(d->second);
            }
        }
        return out;
    };

    // 3. Register, in dependency order. Each step takes the first plugin (by id)
    //    whose dependencies have all registered; when only loops are left, the
    //    first plugin of them. So the order is the same on every computer.
    std::vector<std::size_t> pending;
    for (const auto& [id, i] : byId) pending.push_back(i);  // sorted by id
    std::vector<std::size_t> loadOrder;
    std::set<std::size_t> settled;
    auto needsLoaded = [&](std::size_t i) {
        // A required plugin that has registered but didn't load stops this one.
        auto& info = found[i];
        for (const auto& dep : info.manifest.dependencies) {
            auto d = byId.find(dep);
            if (d == byId.end()) {
                info.issues.push_back(
                    problem("plugin.missing_dependency", displayName(info.manifest) + " needs the plugin " +
                                                             dep + ", which isn't installed. Install " + dep +
                                                             " and restart Khan."));
                return false;
            }
            const auto& needed = found[d->second];
            if (settled.count(d->second) && needed.state != PluginState::Loaded) {
                info.state = PluginState::Skipped;
                info.issues.push_back(
                    problem("plugin.dependency_failed", displayName(info.manifest) + " needs " +
                                                            shortName(needed.manifest) +
                                                            ", which didn't load. Fix that plugin first."));
                return false;
            }
        }
        return true;
    };
    while (!pending.empty()) {
        auto pick = std::find_if(pending.begin(), pending.end(), [&](std::size_t i) {
            for (auto d : depsOf(i)) {
                if (!settled.count(d)) return false;
            }
            return true;
        });
        if (pick == pending.end()) pick = pending.begin();  // only loops left
        const auto i = *pick;
        pending.erase(pick);
        auto& info = found[i];
        if (candidates[i].usable && !needsLoaded(i)) candidates[i].usable = false;
        if (candidates[i].usable) {
            for (const auto& dep : info.manifest.uses) {
                if (!byId.count(dep)) {
                    info.issues.push_back(
                        note("plugin.uses_missing", shortName(info.manifest) + " can use " + dep +
                                                        ", which isn't installed, so what it adds is off."));
                }
            }
            loadOne(candidates[i], info);
        }
        settled.insert(i);
        loadOrder.push_back(i);
    }

    // A plugin in a loop may have registered before a plugin it requires failed:
    // stop it too, and any plugin that requires it, until nothing changes.
    auto cascade = [&] {
        for (bool changed = true; changed;) {
            changed = false;
            for (auto i : loadOrder) {
                auto& info = found[i];
                if (info.state != PluginState::Loaded) continue;
                for (const auto& dep : info.manifest.dependencies) {
                    const auto& needed = found[byId.at(dep)];
                    if (needed.state == PluginState::Loaded) continue;
                    info.state = PluginState::Skipped;
                    info.issues.push_back(problem("plugin.dependency_failed",
                                                  displayName(info.manifest) + " needs " +
                                                      shortName(needed.manifest) +
                                                      ", which didn't load. Fix that plugin first."));
                    withdraw(candidates[i], info);
                    changed = true;
                    break;
                }
            }
        }
    };
    cascade();

    // 4. Start: every service exists now. Run each plugin's onStart callbacks.
    shared_->phase = detail::HostShared::Phase::Start;
    for (auto i : loadOrder) {
        auto& info = found[i];
        auto* hc = candidates[i].context;
        if (info.state != PluginState::Loaded || !hc) continue;
        const auto began = std::chrono::steady_clock::now();
        for (auto& start : hc->starts_) {
            guarded(*hc, start);
            if (!hc->failures_.empty()) break;
        }
        hc->took += std::chrono::steady_clock::now() - began;
        if (!hc->failures_.empty()) {
            for (auto& f : hc->failures_) {
                info.issues.push_back(problem("plugin.start_failed",
                                              displayName(info.manifest) + " couldn't start. " + f,
                                              info.manifest.id));
            }
            info.state = PluginState::Failed;
            withdraw(candidates[i], info);
            cascade();
        }
    }

    // 5. Check every loaded plugin's commands and menu items, then add them.
    //    A menu item may run any loaded plugin's command, so check until nothing
    //    more fails, then add what's left.
    for (bool changed = true; changed;) {
        changed = false;
        std::set<std::string> commandNames, actionIds;
        for (auto i : loadOrder) {
            const auto* hc = candidates[i].context;
            if (found[i].state != PluginState::Loaded || !hc) continue;
            for (const auto* cmd : hc->staged_.commands()) commandNames.insert(cmd->name);
        }
        std::set<std::string> takenCommands;
        for (auto i : loadOrder) {
            auto& info = found[i];
            auto* hc = candidates[i].context;
            if (info.state != PluginState::Loaded || !hc) continue;
            const auto& m = info.manifest;
            std::vector<std::string> failures;
            for (const auto* cmd : hc->staged_.commands()) {
                if (!info.builtin && !startsWith(cmd->name, m.id + ".")) {
                    failures.push_back("Its command " + cmd->name + " must start with \"" + m.id +
                                       ".\". Ask its author for a fixed version.");
                } else if (!takenCommands.insert(cmd->name).second) {
                    failures.push_back("Its command " + cmd->name +
                                       " has the same name as a command another plugin already added. "
                                       "Turn off one of the two plugins.");
                }
            }
            for (const auto& a : hc->stagedActions_) {
                if (!isValidId(a.id) || (!info.builtin && !startsWith(a.id, m.id + "."))) {
                    failures.push_back("Its menu item \"" + a.text + "\" has the id \"" + a.id +
                                       "\", which must start with \"" + m.id +
                                       ".\". Ask its author for a fixed version.");
                } else if (!actionIds.insert(a.id).second) {
                    failures.push_back("Its menu item \"" + a.text + "\" has the same id as another one (" +
                                       a.id + "). Ask its author for a fixed version.");
                } else if (a.command.empty() && !a.run) {
                    failures.push_back("Its menu item \"" + a.text +
                                       "\" does nothing: it has no command and no run function. Ask its "
                                       "author for a fixed version.");
                } else if (a.run && !a.command.empty()) {
                    failures.push_back("Its menu item \"" + a.text +
                                       "\" has both a command and a run function; it can only have one. "
                                       "Ask its author for a fixed version.");
                } else if (!a.run && !commandNames.count(a.command)) {
                    failures.push_back("Its menu item \"" + a.text + "\" runs a command, " + a.command +
                                       ", that no loaded plugin has. Check its \"requires\" list.");
                }
            }
            if (failures.empty()) continue;
            for (auto& f : failures) {
                // f is a whole sentence: from fail() it is the plugin's own words.
                info.issues.push_back(
                    problem("plugin.register_failed", displayName(m) + " couldn't start. " + f, m.id));
            }
            info.state = PluginState::Failed;
            withdraw(candidates[i], info);
            cascade();
            changed = true;
            break;
        }
    }
    for (auto i : loadOrder) {
        auto& info = found[i];
        auto* hc = candidates[i].context;
        if (info.state != PluginState::Loaded || !hc) continue;
        std::vector<std::string> names;
        for (const auto* cmd : hc->staged_.commands()) {
            registry_.add(*cmd, *hc->staged_.factory(cmd->name));
            names.push_back(cmd->name);
        }
        info.commands = std::move(names);
        for (auto& a : hc->stagedActions_) {
            info.actions.push_back(a.id);
            actions_.push_back(std::move(a));
        }
        hc->stagedActions_.clear();
        if (slowStartLimit_.count() > 0 && hc->took > slowStartLimit_) {
            info.issues.push_back(warning("plugin.slow_start",
                                          shortName(info.manifest) + " took " + milliseconds(hc->took) +
                                              " to start, which slows Khan's start. Do slow work later, "
                                              "when it's first needed.",
                                          "limit " + std::to_string(slowStartLimit_.count()) + " ms"));
        }
    }
    shared_->phase = detail::HostShared::Phase::Running;
    shared_->manifests.clear();  // they point into `found`, which is about to move

    // 6. List them: loaded ones in load order, then the rest.
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

    auto ctx = std::make_unique<HostContext>(info, registry_, bus_, documents_, selection_, *shared_,
                                             *settingsBackend_, overlays_);
    auto& hc = *ctx;
    c.context = &hc;
    const auto began = std::chrono::steady_clock::now();
    guarded(hc, [&] { fn(hc); });
    hc.took = std::chrono::steady_clock::now() - began;
    hc.registering_ = false;

    // Its services become available to the plugins that register after it, and
    // to every plugin's onStart.
    std::vector<std::string> provided;
    if (hc.failures_.empty()) {
        for (auto& [name, service] : hc.stagedServices_) {
            auto taken = shared_->services.find(name);
            if (taken != shared_->services.end()) {
                const auto& owner = taken->second.provider;
                const std::string ownerName = owner.empty() ? "Khan"
                                                            : (shared_->manifests.count(owner)
                                                                   ? shortName(*shared_->manifests.at(owner))
                                                                   : owner);
                hc.warnOnce(warning("plugin.service_taken",
                                    shortName(m) + " tried to provide " + name + ", which " + ownerName +
                                        " already provides. Khan kept " + ownerName + "'s."));
                continue;
            }
            shared_->services[name] = service;
            provided.push_back(name);
        }
    }
    hc.stagedServices_.clear();
    std::sort(provided.begin(), provided.end());
    info.services = std::move(provided);

    if (!hc.failures_.empty()) {
        for (auto& f : hc.failures_) {
            // f is a whole sentence: from fail() it is the plugin's own words.
            failed(problem("plugin.register_failed", who + " couldn't start. " + f, m.id));
        }
        hc.subscriptions_.clear();    // disconnect its listeners now
        overlays_.removeOwner(m.id);  // and stop drawing what it drew
        contexts_.push_back(std::move(ctx));
        return;
    }
    info.state = PluginState::Loaded;
    contexts_.push_back(std::move(ctx));
}

void PluginHost::withdraw(Candidate& c, PluginInfo& info) {
    // Take back what a plugin offered when it stops after registering: its
    // listeners and services. Its commands and menu items were never added.
    if (!c.context) return;
    c.context->subscriptions_.clear();
    for (auto it = shared_->services.begin(); it != shared_->services.end();) {
        it = it->second.provider == info.manifest.id ? shared_->services.erase(it) : std::next(it);
    }
    info.services.clear();
    overlays_.removeOwner(info.manifest.id);
}

}  // namespace kh::plugin
