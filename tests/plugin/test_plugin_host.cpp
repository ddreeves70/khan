// Tests for finding, checking and loading plugins (kh_plugin).

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "../model/FakeKernel.h"
#include "kh/plugin/PluginHost.h"
#include "kh/plugins/Modeling.h"

using namespace kh;
using namespace kh::plugin;

namespace {

std::string manifest(const std::string& id, const std::string& requires_ = "", int api = kPluginApiVersion) {
    return R"({"id": ")" + id + R"(", "name": ")" + id + R"(", "version": "1.0.0", "apiVersion": )" +
           std::to_string(api) + R"(, "requires": [)" + requires_ + "]}";
}

// Built-in test plugins record the order they were registered in.
std::vector<std::string>* order = nullptr;

void recordA(PluginContext& ctx) {
    order->push_back(ctx.manifest().id);
}

void addThing(PluginContext& ctx) {
    order->push_back(ctx.manifest().id);
    ctx.addCommand({"test.thing", "Thing", "Does nothing.", {}},
                   [](const model::Document&, const model::Args&) -> std::unique_ptr<model::Command> {
                       return nullptr;
                   });
}

void throws(PluginContext& ctx) {
    ctx.addCommand({"test.never", "Never", "Never added.", {}},
                   [](const model::Document&, const model::Args&) -> std::unique_ptr<model::Command> {
                       return nullptr;
                   });
    ctx.listen<DocumentChanged>([](const DocumentChanged&) {});
    throw std::runtime_error("no config file");
}

void fails(PluginContext& ctx) {
    ctx.listen<DocumentChanged>([](const DocumentChanged&) {});
    ctx.fail("It needs a 3D printer to be connected.");
}

const PluginInfo& get(const PluginHost& host, const char* id) {
    const auto* p = host.find(id);
    REQUIRE(p);
    return *p;
}

std::string firstCode(const PluginInfo& p) {
    return p.issues.empty() ? "" : p.issues.front().code;
}

std::vector<std::string> loadedIds(const PluginHost& host) {
    std::vector<std::string> ids;
    for (const auto& p : host.plugins()) {
        if (p.state == PluginState::Loaded) ids.push_back(p.manifest.id);
    }
    return ids;
}

struct OrderGuard {
    std::vector<std::string> seen;
    OrderGuard() { order = &seen; }
    ~OrderGuard() { order = nullptr; }
};

}  // namespace

TEST_CASE("kh.modeling loads and its commands work", "[plugin][host]") {
    test::FakeKernel kernel;
    PluginHost host(&kernel);
    host.addBuiltin(plugins::modelingPlugin());
    host.loadAll();

    const auto& modeling = get(host, "kh.modeling");
    CHECK(modeling.state == PluginState::Loaded);
    CHECK(modeling.builtin);
    CHECK(modeling.commands == std::vector<std::string>{"feature.add_boolean", "feature.add_box",
                                                        "feature.add_cylinder", "feature.delete",
                                                        "feature.rename", "feature.set_material",
                                                        "material.define", "material.delete"});

    const DocId doc = host.documents().add(std::make_unique<model::Document>());
    REQUIRE(host.documents().run(doc, "feature.add_box", {{"size", kernel::Vec3{2, 3, 4}}}).empty());
    const auto* report = host.documents().lastRebuild(doc);
    REQUIRE(report);
    CHECK(report->ok());
    CHECK(kernel.massProps(*report->shapeOf(report->bodies().at(0))).volume == 24.0);  // 2 * 3 * 4 mm^3
}

TEST_CASE("plugins load after the plugins they need", "[plugin][host]") {
    OrderGuard guard;
    PluginHost host;
    // Added in the "wrong" order on purpose.
    host.addBuiltin({manifest("test.c", R"("test.b")"), &recordA});
    host.addBuiltin({manifest("test.b", R"("test.a")"), &recordA});
    host.addBuiltin({manifest("test.a"), &recordA});
    host.addBuiltin({manifest("test.z"), &recordA});
    host.loadAll();
    // Ids in order, each one as soon as what it needs has loaded: the same on every computer.
    CHECK(guard.seen == std::vector<std::string>{"test.a", "test.b", "test.c", "test.z"});
    CHECK(loadedIds(host) == guard.seen);  // listed in load order
}

TEST_CASE("one bad plugin doesn't stop the others", "[plugin][host]") {
    OrderGuard guard;
    PluginHost host;
    host.addBuiltin({manifest("test.good"), &recordA});
    host.addBuiltin({manifest("test.needs_missing", R"("test.not_installed")"), &recordA});
    host.addBuiltin({manifest("test.old", "", kPluginApiVersion + 1), &recordA});
    host.addBuiltin({manifest("test.needs_old", R"("test.old")"), &recordA});
    host.addBuiltin({"{ damaged", &recordA});
    host.loadAll();

    CHECK(guard.seen == std::vector<std::string>{"test.good"});
    CHECK(get(host, "test.good").state == PluginState::Loaded);

    const auto& missing = get(host, "test.needs_missing");
    CHECK(missing.state == PluginState::Failed);
    CHECK(firstCode(missing) == "plugin.missing_dependency");
    CHECK(missing.issues[0].message.find("test.not_installed") != std::string::npos);

    const auto& old = get(host, "test.old");
    CHECK(firstCode(old) == "plugin.api_version");
    CHECK(old.issues[0].message.find("different version of Khan") != std::string::npos);

    const auto& needsOld = get(host, "test.needs_old");
    CHECK(needsOld.state == PluginState::Skipped);
    CHECK(firstCode(needsOld) == "plugin.dependency_failed");

    // The damaged one is listed too, so the Plugin manager can show it.
    CHECK(host.plugins().size() == 5);
    CHECK(host.plugins().front().manifest.id == "test.good");  // loaded ones first
}

TEST_CASE("plugins that need each other load anyway, with a warning", "[plugin][host]") {
    OrderGuard guard;
    PluginHost host;
    host.addBuiltin({manifest("test.egg", R"("test.chicken")"), &recordA});
    host.addBuiltin({manifest("test.chicken", R"("test.egg")"), &recordA});
    host.loadAll();
    CHECK(guard.seen == std::vector<std::string>{"test.chicken", "test.egg"});  // id order inside a loop
    CHECK(get(host, "test.egg").state == PluginState::Loaded);
    CHECK(get(host, "test.chicken").state == PluginState::Loaded);
    // One warning, on the loop's first plugin, naming the whole loop.
    const auto& first = get(host, "test.chicken");
    REQUIRE(first.issues.size() == 1);
    CHECK(first.issues[0].code == "plugin.dependency_loop");
    CHECK(first.issues[0].severity == Severity::Warning);
    CHECK(first.issues[0].message.find("test.chicken → test.egg → test.chicken") != std::string::npos);
    CHECK(get(host, "test.egg").issues.empty());
}

TEST_CASE("only the first of two plugins with one id loads", "[plugin][host]") {
    OrderGuard guard;
    PluginHost host;
    host.addBuiltin({manifest("test.same"), &recordA});
    host.addBuiltin({manifest("test.same"), &recordA});
    host.loadAll();
    CHECK(guard.seen.size() == 1);
    REQUIRE(host.plugins().size() == 2);
    CHECK(firstCode(host.plugins()[1]) == "plugin.duplicate");
}

TEST_CASE("a plugin that throws or fails leaves nothing behind", "[plugin][host]") {
    PluginHost host;
    host.addBuiltin({manifest("test.throws"), &throws});
    host.addBuiltin({manifest("test.fails"), &fails});
    const auto before = host.events().listenerCount(DocumentChanged::kType);  // Khan's own (the selection)
    host.loadAll();

    const auto& t = get(host, "test.throws");
    CHECK(t.state == PluginState::Failed);
    CHECK(firstCode(t) == "plugin.register_failed");
    CHECK(t.issues[0].message.find("no config file") != std::string::npos);
    CHECK(host.commands().info("test.never") == nullptr);

    const auto& f = get(host, "test.fails");
    CHECK(f.state == PluginState::Failed);
    CHECK(f.issues[0].message.find("3D printer") != std::string::npos);

    // Neither plugin's listener is still connected.
    CHECK(host.events().listenerCount(DocumentChanged::kType) == before);
}

TEST_CASE("two plugins can't add the same command", "[plugin][host]") {
    OrderGuard guard;
    PluginHost host;
    host.addBuiltin({manifest("test.first"), &addThing});
    host.addBuiltin({manifest("test.second"), &addThing});
    host.loadAll();
    CHECK(get(host, "test.first").state == PluginState::Loaded);
    CHECK(get(host, "test.first").commands == std::vector<std::string>{"test.thing"});
    const auto& second = get(host, "test.second");
    CHECK(second.state == PluginState::Failed);
    CHECK(second.issues[0].message.find("same name") != std::string::npos);
}

TEST_CASE("broken outside plugins are refused with a plain message", "[plugin][host]") {
    PluginHost host;
    host.addBuiltin(plugins::modelingPlugin());
    host.addFolder(KH_TEST_PLUGIN_FOLDER);
    host.addFolder("this/folder/does/not/exist");  // skipped quietly
    host.loadAll();

    CHECK(get(host, "kh.modeling").state == PluginState::Loaded);
    CHECK(firstCode(get(host, "test.noentry")) == "plugin.library_bad");
    CHECK(get(host, "test.noentry").issues[0].message.find("KH_PLUGIN_ENTRY") != std::string::npos);
    CHECK(firstCode(get(host, "test.oldapi")) == "plugin.api_version");
    const auto& bad = get(host, "test.badname");
    CHECK(firstCode(bad) == "plugin.register_failed");
    CHECK(bad.issues[0].message.find("must start with \"test.badname.\"") != std::string::npos);
    // The built-in command it tried to take is still kh.modeling's.
    CHECK(host.commands().info("feature.add_box")->label == "Add box");
}

TEST_CASE("outside plugins can't use Khan's ids or skip their library", "[plugin][host]") {
    const auto dir = std::filesystem::temp_directory_path() / "kh_plugin_host_test";
    std::filesystem::remove_all(dir);
    auto write = [&](const std::string& folder, const std::string& json) {
        std::filesystem::create_directories(dir / folder);
        std::ofstream(dir / folder / "plugin.json") << json;
    };
    const std::string api = std::to_string(kPluginApiVersion);
    write("fake", R"({"id": "kh.fake", "name": "Fake", "version": "1", "apiVersion": )" + api +
                      R"(, "library": "fake"})");
    write("nolib",
          R"({"id": "test.nolib", "name": "No library", "version": "1", "apiVersion": )" + api + "}");
    write("gone", R"({"id": "test.gone", "name": "Gone", "version": "1", "apiVersion": )" + api +
                      R"(, "library": "gone"})");
    write("broken", "{ not json");
    std::filesystem::create_directories(dir / "not_a_plugin");  // no plugin.json: ignored

    PluginHost host;
    host.addFolder(dir);
    host.loadAll();
    CHECK(firstCode(get(host, "kh.fake")) == "plugin.reserved_id");
    CHECK(get(host, "kh.fake").issues[0].message.find("yourname.fake") != std::string::npos);
    CHECK(firstCode(get(host, "test.nolib")) == "plugin.no_library");
    CHECK(firstCode(get(host, "test.gone")) == "plugin.library_missing");
    CHECK(get(host, "test.gone").issues[0].message.find(libraryFileName("gone")) != std::string::npos);
    CHECK(host.plugins().size() == 4);  // the damaged one is listed by folder name

    std::filesystem::remove_all(dir);
}

TEST_CASE("library file names follow the system", "[plugin][host]") {
#if defined(_WIN32)
    CHECK(libraryFileName("hello") == "hello.dll");
#elif defined(__APPLE__)
    CHECK(libraryFileName("hello") == "hello.dylib");
#else
    CHECK(libraryFileName("hello") == "hello.so");
#endif
}

namespace {

struct FakeService {
    static constexpr const char* kServiceName = "test.fake";
    int value = 42;
};

int seenService = 0;

void usesService(PluginContext& ctx) {
    auto* fake = ctx.service<FakeService>();
    seenService = fake ? fake->value : -1;
}

void goodAction(PluginContext& ctx) {
    ctx.addAction({.id = "test.actions.box", .text = "Box", .menu = "Create", .command = "feature.add_box"});
}
void badActionId(PluginContext& ctx) {
    ctx.addAction({.id = "Not An Id", .text = "Box", .menu = "Create", .command = "feature.add_box"});
}
void unknownCommand(PluginContext& ctx) {
    ctx.addAction(
        {.id = "test.unknown.go", .text = "Go", .menu = "Create", .command = "test.unknown.nothing"});
}

}  // namespace

TEST_CASE("plugins find services the program provides", "[plugin][host]") {
    FakeService fake;
    {
        PluginHost host;
        host.provide(fake);
        host.addBuiltin({manifest("test.service"), &usesService});
        host.loadAll();
        CHECK(seenService == 42);
    }
    PluginHost cli;  // like kh_cli: nothing provided
    cli.addBuiltin({manifest("test.service"), &usesService});
    cli.loadAll();
    CHECK(seenService == -1);
}

TEST_CASE("kh.modeling adds menu items that run its commands", "[plugin][host]") {
    PluginHost host;
    host.addBuiltin(plugins::modelingPlugin());
    host.loadAll();
    std::vector<std::string> ids;
    for (const auto& a : host.actions()) {
        ids.push_back(a.id);
        CHECK(a.plugin == "kh.modeling");
        CHECK(host.commands().info(a.command) != nullptr);
        CHECK_FALSE(a.tip.empty());
    }
    CHECK(ids == std::vector<std::string>{"kh.modeling.box", "kh.modeling.cylinder", "kh.modeling.union",
                                          "kh.modeling.cut", "kh.modeling.intersect", "kh.modeling.delete"});
    CHECK(get(host, "kh.modeling").actions == ids);
}

TEST_CASE("bad actions stop a plugin from loading", "[plugin][host]") {
    PluginHost host;
    host.addBuiltin(plugins::modelingPlugin());
    host.addBuiltin({manifest("test.actions", R"("kh.modeling")"), &goodAction});
    host.addBuiltin({manifest("test.again", R"("kh.modeling")"), &goodAction});  // same action id
    host.addBuiltin({manifest("test.badid", R"("kh.modeling")"), &badActionId});
    host.addBuiltin({manifest("test.unknown"), &unknownCommand});
    host.loadAll();

    CHECK(get(host, "test.actions").state == PluginState::Loaded);
    CHECK(get(host, "test.again").issues.at(0).message.find("same id") != std::string::npos);
    CHECK(get(host, "test.badid").issues.at(0).message.find("must start with") != std::string::npos);
    CHECK(get(host, "test.unknown").issues.at(0).message.find("no loaded plugin has") != std::string::npos);
    // Nothing from the failed plugins is left behind.
    std::size_t fromTests = 0;
    for (const auto& a : host.actions()) fromTests += a.plugin.rfind("test.", 0) == 0 ? 1 : 0;
    CHECK(fromTests == 1);
}
