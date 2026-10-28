#pragma once

/// @file
/// Commands by name, with typed and described arguments.

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

/// The type of a command argument.
enum class ArgType {
    Number,       ///< A double (an integer is accepted too).
    Integer,      ///< A std::int64_t.
    Bool,         ///< A bool.
    Text,         ///< A std::string.
    Vec3,         ///< A kernel::Vec3, in millimeters.
    FeatureRef,   ///< A FeatureId of a feature in the document.
    Choice,       ///< A std::string, one of ArgSpec::choices.
    FeatureList,  ///< A std::vector<FeatureId>: one or more steps in the document, in order.
    FolderRef,    ///< A FolderId of a folder in the document.
};

/// One argument value. Which alternative is allowed depends on the ArgSpec's ArgType.
using ArgValue = std::variant<double, std::int64_t, bool, std::string, kernel::Vec3, FeatureId,
                              std::vector<FeatureId>, FolderId>;

/// Arguments for one command, by argument name.
using Args = std::map<std::string, ArgValue>;

/// Describes one argument of a command.
struct ArgSpec {
    std::string name;                  ///< Argument name, e.g. "size".
    ArgType type;                      ///< What kind of value it takes.
    std::string description;           ///< Plain-language help, with units.
    bool required = true;              ///< False if the command has a default for it.
    std::vector<std::string> choices;  ///< Allowed values, for ArgType::Choice only.
};

/// Describes a command for scripts, plugins, the AI assistant and command search.
struct CommandInfo {
    std::string name;           ///< Stable id, e.g. "feature.add_box". Never renamed.
    std::string label;          ///< Shown to people, e.g. "Add box".
    std::string description;    ///< One or two plain sentences.
    std::vector<ArgSpec> args;  ///< The arguments, in the order they are usually shown.
};

/// Builds a command from arguments that the registry has already checked.
using CommandFactory = std::function<std::unique_ptr<Command>(const Document&, const Args&)>;

/// What CommandRegistry::create() returns.
struct CreateResult {
    std::unique_ptr<Command> command;  ///< The command, ready for Document::execute(); null on failure.
    Diagnostics issues;                ///< Every problem with the arguments, all at once.

    /// Checks whether a command was made.
    /// @return True if @ref command is set.
    bool ok() const { return command != nullptr; }
};

/// Every command that can be run by name.
///
/// The UI, Python, plugins and the AI assistant all create commands here, so
/// they share one checked path.
///
/// @code
/// auto made = CommandRegistry::builtin().create(
///     "feature.add_box", {{"size", kernel::Vec3{10, 20, 30}}}, doc);
/// if (made.ok()) doc.execute(std::move(made.command));
/// @endcode
class CommandRegistry {
public:
    /// Adds a command, or replaces one with the same name.
    /// @param info The command's name, label, description and arguments.
    /// @param factory Builds the command once its arguments have been checked.
    void add(CommandInfo info, CommandFactory factory);

    /// Looks up a command's description.
    /// @param name The command name, e.g. "feature.add_box".
    /// @return The description, or nullptr if there is no such command.
    const CommandInfo* info(std::string_view name) const;

    /// Lists every command.
    /// @return The descriptions, sorted by name.
    std::vector<const CommandInfo*> commands() const;

    /// Gives the function that builds a command, so commands can be copied into
    /// another registry (the plugin host does this when a plugin loads).
    /// @param name The command name.
    /// @return The factory, or nullptr if there is no such command.
    const CommandFactory* factory(std::string_view name) const;

    /// Checks arguments and builds a command.
    ///
    /// Checks for an unknown command, unknown arguments, missing required
    /// arguments, wrong types, values outside the choices, and features that are
    /// not in @p doc. The command still runs its own checks when executed.
    /// @param name The command name.
    /// @param args The argument values.
    /// @param doc The document the command will run on.
    /// @return The command, or every problem found.
    CreateResult create(std::string_view name, const Args& args, const Document& doc) const;

    /// Gives the registry with all built-in commands.
    /// @return The shared built-in registry.
    static const CommandRegistry& builtin();

private:
    struct Entry {
        CommandInfo info;
        CommandFactory factory;
    };
    std::map<std::string, Entry, std::less<>> entries_;
};

/// Reads a number argument, for use in a CommandFactory.
/// @param args The checked arguments.
/// @param name The argument name.
/// @param fallback Returned if the argument is absent.
/// @return The value; an integer is converted to double.
double argNumber(const Args& args, const std::string& name, double fallback = 0);

/// Reads a text or choice argument, for use in a CommandFactory.
/// @param args The checked arguments.
/// @param name The argument name.
/// @param fallback Returned if the argument is absent.
/// @return The value.
std::string argText(const Args& args, const std::string& name, const std::string& fallback = {});

/// Reads a Vec3 argument, for use in a CommandFactory.
/// @param args The checked arguments.
/// @param name The argument name.
/// @param fallback Returned if the argument is absent.
/// @return The value (mm).
kernel::Vec3 argVec3(const Args& args, const std::string& name, const kernel::Vec3& fallback = {});

/// Reads a feature argument, for use in a CommandFactory.
/// @param args The checked arguments.
/// @param name The argument name.
/// @return The feature id, or a nil id if the argument is absent.
FeatureId argFeature(const Args& args, const std::string& name);

/// Reads a list-of-steps argument, for use in a CommandFactory.
/// @param args The checked arguments.
/// @param name The argument name.
/// @return The feature ids, in order; empty if the argument is absent.
std::vector<FeatureId> argFeatures(const Args& args, const std::string& name);

/// Reads a folder argument, for use in a CommandFactory.
/// @param args The checked arguments.
/// @param name The argument name.
/// @return The folder id, or a nil id if the argument is absent.
FolderId argFolder(const Args& args, const std::string& name);

/// Reads a true-or-false argument, for use in a CommandFactory.
/// @param args The checked arguments.
/// @param name The argument name.
/// @param fallback Returned if the argument is absent.
/// @return The value.
bool argBool(const Args& args, const std::string& name, bool fallback);

}  // namespace kh::model
