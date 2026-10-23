#include "kh/plugin/Manifest.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace kh::plugin {

namespace {

using nlohmann::json;

std::string where(const std::filesystem::path& folder) {
    return folder.empty() ? std::string("a built-in plugin") : "the plugin in " + folder.string();
}

bool isWordChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

}  // namespace

bool isValidId(std::string_view id) {
    std::size_t parts = 0;
    std::size_t len = 0;
    for (char c : id) {
        if (c == '.') {
            if (len == 0) return false;  // empty part: ".a", "a..b"
            ++parts;
            len = 0;
        } else if (isWordChar(c)) {
            ++len;
        } else {
            return false;
        }
    }
    if (len == 0) return false;  // trailing dot or empty
    return parts + 1 >= 2;
}

ManifestResult parseManifest(std::string_view text, const std::filesystem::path& folder) {
    ManifestResult result;
    auto problem = [&](std::string message, std::string detail = {}) {
        result.issues.push_back(
            {"plugin.manifest_bad", Severity::Error, std::move(message), std::move(detail)});
    };
    const std::string fix = " Ask the plugin's author for a fixed version.";

    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        problem("The plugin.json file of " + where(folder) + " is damaged, so Khan can't load it." + fix,
                "plugin.json is not a JSON object");
        return result;
    }

    PluginManifest m;
    m.folder = folder;
    auto text_ = [&](const char* key, std::string& out, bool required) {
        if (!j.contains(key)) {
            if (required)
                problem("The plugin.json of " + where(folder) + " is missing \"" + key + "\"." + fix);
            return;
        }
        if (!j[key].is_string()) {
            problem("In the plugin.json of " + where(folder) + ", \"" + key + "\" must be text." + fix);
            return;
        }
        out = j[key].get<std::string>();
        if (required && out.empty())
            problem("The plugin.json of " + where(folder) + " has an empty \"" + key + "\"." + fix);
    };
    auto list = [&](const char* key, std::vector<std::string>& out) {
        if (!j.contains(key)) return;
        if (!j[key].is_array()) {
            problem("In the plugin.json of " + where(folder) + ", \"" + key + "\" must be a list of text." +
                    fix);
            return;
        }
        for (const auto& v : j[key]) {
            if (!v.is_string()) {
                problem("In the plugin.json of " + where(folder) + ", \"" + key +
                        "\" must be a list of text." + fix);
                return;
            }
            out.push_back(v.get<std::string>());
        }
    };

    text_("id", m.id, true);
    text_("name", m.name, true);
    text_("version", m.version, true);
    text_("description", m.description, false);
    text_("library", m.library, false);
    list("requires", m.dependencies);
    list("permissions", m.permissions);

    if (!j.contains("apiVersion")) {
        problem("The plugin.json of " + where(folder) + " is missing \"apiVersion\"." + fix);
    } else if (!j["apiVersion"].is_number_integer()) {
        problem("In the plugin.json of " + where(folder) + ", \"apiVersion\" must be a whole number." + fix);
    } else {
        m.apiVersion = j["apiVersion"].get<int>();
    }

    if (!m.id.empty() && !isValidId(m.id)) {
        problem("The plugin id \"" + m.id +
                "\" isn't allowed. Ids use lower-case letters, digits and _, in parts "
                "joined by dots, like \"example.hello\"." +
                fix);
    }
    for (const auto& dep : m.dependencies) {
        if (!isValidId(dep)) {
            problem("The plugin " + (m.name.empty() ? m.id : m.name) + " needs a plugin called \"" + dep +
                    "\", which isn't a valid plugin id." + fix);
        }
    }

    if (result.issues.empty()) result.manifest = std::move(m);
    return result;
}

ManifestResult readManifest(const std::filesystem::path& folder) {
    std::ifstream f(folder / "plugin.json", std::ios::binary);
    if (!f) {
        ManifestResult r;
        r.issues.push_back(
            {"plugin.manifest_missing", Severity::Error,
             "Khan couldn't read plugin.json in " + folder.string() + ". Every plugin folder needs one.",
             (folder / "plugin.json").string()});
        return r;
    }
    std::ostringstream text;
    text << f.rdbuf();
    return parseManifest(text.str(), folder);
}

}  // namespace kh::plugin
