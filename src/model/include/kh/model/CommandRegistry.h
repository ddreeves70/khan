#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "kh/core/Diagnostic.h"
#include "kh/core/Id.h"
#include "kh/kernel/Types.h"
#include "kh/model/Command.h"

namespace kh::model {

class Document;

enum class ArgType {
    Number,      // double (an integer is accepted too)
    Integer,     // std::int64_t
    Bool,        // bool
    Text,        // std::string
    Vec3,        // kernel::Vec3, millimeters
    FeatureRef,  // FeatureId of a feature in the document
    Choice,      // std::string, one of ArgSpec::choices
};

using ArgValue = std::variant<double, std::int64_t, bool, std::string, kernel::Vec3, FeatureId>;
using Args = std::map<std::string, ArgValue>;

struct ArgSpec {
    std::string name;
    ArgType type;
    std::string description;
    bool required = true;
    std::vector<std::string> choices;  // for ArgType::Choice
};

// Describes a command for scripts, plugins, the AI assistant and command search.
struct CommandInfo {
    std::string name;         // stable id, e.g. "feature.add_box"
    std::string label;        // shown to people, e.g. "Add box"
    std::string description;  // one or two plain sentences
    std::vector<ArgSpec> args;
};

// Builds a command from arguments that the registry has already checked.
using CommandFactory = std::function<std::unique_ptr<Command>(const Document&, const Args&)>;

struct CreateResult {
    std::unique_ptr<Command> command;
    Diagnostics issues;
    bool ok() const { return command != nullptr; }
};

// Every command that can be run by name. The UI, Python, plugins and the AI
// assistant all create commands here, so they share one checked path.
class CommandRegistry {
public:
    void add(CommandInfo info, CommandFactory factory);

    const CommandInfo* info(std::string_view name) const;
    std::vector<const CommandInfo*> commands() const;  // sorted by name

    // Checks names and types of `args` against the command's ArgSpecs, then builds it.
    CreateResult create(std::string_view name, const Args& args, const Document& doc) const;

    // The registry with all built-in commands.
    static const CommandRegistry& builtin();

private:
    struct Entry {
        CommandInfo info;
        CommandFactory factory;
    };
    std::map<std::string, Entry, std::less<>> entries_;
};

// Helpers for factories: read an argument that the registry has already checked.
double argNumber(const Args& args, const std::string& name, double fallback = 0);
std::string argText(const Args& args, const std::string& name, const std::string& fallback = {});
kernel::Vec3 argVec3(const Args& args, const std::string& name, const kernel::Vec3& fallback = {});
FeatureId argFeature(const Args& args, const std::string& name);

}  // namespace kh::model
