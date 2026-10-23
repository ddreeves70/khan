// Tests for reading plugin.json (kh_plugin).

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "kh/plugin/Manifest.h"

using namespace kh::plugin;

namespace {

bool mentions(const ManifestResult& r, const std::string& text) {
    for (const auto& d : r.issues) {
        if (d.message.find(text) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("a complete manifest is read", "[plugin][manifest]") {
    const auto r = parseManifest(R"({
        "id": "example.hello", "name": "Hello", "version": "1.0.0", "apiVersion": 1,
        "description": "Adds a cube.", "library": "hello",
        "requires": ["kh.modeling"], "permissions": ["files.read"],
        "somethingNew": {"ignored": true}
    })",
                                 "plugins/hello");
    REQUIRE(r.ok());
    CHECK(r.issues.empty());
    const auto& m = *r.manifest;
    CHECK(m.id == "example.hello");
    CHECK(m.name == "Hello");
    CHECK(m.version == "1.0.0");
    CHECK(m.apiVersion == 1);
    CHECK(m.description == "Adds a cube.");
    CHECK(m.library == "hello");
    CHECK(m.dependencies == std::vector<std::string>{"kh.modeling"});
    CHECK(m.permissions == std::vector<std::string>{"files.read"});
    CHECK(m.folder == "plugins/hello");
}

TEST_CASE("optional keys may be left out", "[plugin][manifest]") {
    const auto r = parseManifest(R"({"id": "a.b", "name": "A", "version": "1", "apiVersion": 1})");
    REQUIRE(r.ok());
    CHECK(r.manifest->dependencies.empty());
    CHECK(r.manifest->library.empty());
}

TEST_CASE("broken manifests list every problem", "[plugin][manifest]") {
    SECTION("not JSON") {
        const auto r = parseManifest("{ nope", "plugins/x");
        CHECK_FALSE(r.ok());
        CHECK(r.issues.at(0).code == "plugin.manifest_bad");
        CHECK(mentions(r, "damaged"));
    }
    SECTION("missing keys, all reported at once") {
        const auto r = parseManifest(R"({"name": "Hello"})");
        CHECK_FALSE(r.ok());
        CHECK(r.issues.size() == 3);  // id, version, apiVersion
        CHECK(mentions(r, "\"id\""));
        CHECK(mentions(r, "\"version\""));
        CHECK(mentions(r, "\"apiVersion\""));
    }
    SECTION("wrong types") {
        const auto r = parseManifest(
            R"({"id": "a.b", "name": 5, "version": "1", "apiVersion": "one", "requires": "kh.modeling"})");
        CHECK_FALSE(r.ok());
        CHECK(mentions(r, "\"name\" must be text"));
        CHECK(mentions(r, "whole number"));
        CHECK(mentions(r, "list of text"));
    }
    SECTION("a badly formed id") {
        const auto r =
            parseManifest(R"({"id": "Hello World", "name": "H", "version": "1", "apiVersion": 1})");
        CHECK_FALSE(r.ok());
        CHECK(mentions(r, "isn't allowed"));
    }
}

TEST_CASE("ids are lower-case words joined by dots", "[plugin][manifest]") {
    CHECK(isValidId("kh.modeling"));
    CHECK(isValidId("example.hello.add_cube"));
    CHECK(isValidId("a1.b_2"));
    CHECK_FALSE(isValidId("hello"));  // needs two parts
    CHECK_FALSE(isValidId(""));
    CHECK_FALSE(isValidId("Example.hello"));
    CHECK_FALSE(isValidId("example..hello"));
    CHECK_FALSE(isValidId(".hello"));
    CHECK_FALSE(isValidId("hello."));
    CHECK_FALSE(isValidId("ex-ample.hello"));
}

TEST_CASE("a folder without plugin.json says so", "[plugin][manifest]") {
    const auto r = readManifest("this/folder/does/not/exist");
    CHECK_FALSE(r.ok());
    CHECK(r.issues.at(0).code == "plugin.manifest_missing");
}
