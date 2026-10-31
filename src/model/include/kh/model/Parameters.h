#pragma once

/// @file
/// The design's parameters: named values like `wall = 3 mm` or
/// `height = wall * 4`, worked out in the order they use each other.

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kh/core/Diagnostic.h"
#include "kh/core/Id.h"
#include "kh/model/Expression.h"

namespace kh::model {

/// One named value in a design, such as `wall = 3 mm`.
struct Parameter {
    ParamId id;              ///< Never changes, even when it's renamed.
    std::string name;        ///< What formulas call it, like "wall".
    std::string expression;  ///< Its formula as typed, like "3 mm" or "wall * 4".
    std::string note;        ///< What it's for, in the student's words. May be empty.
};

/// What one parameter worked out to.
struct ParameterValue {
    std::optional<Quantity> value;  ///< The value, if it could be worked out.
    Diagnostics issues;             ///< Why not, in plain words; empty if it worked.

    /// Checks whether it worked.
    /// @return True if there's a value.
    bool ok() const { return value.has_value(); }
};

/// Every parameter's value, from ParameterTable::evaluate().
class ParameterValues {
public:
    /// Finds one parameter's value.
    /// @param name Its name (upper and lower case must match).
    /// @return The value, or nullptr if there's no parameter by that name.
    const ParameterValue* find(std::string_view name) const;

    /// Lists the parameters in the order they were worked out: each after
    /// every parameter it uses.
    /// @return Their names.
    const std::vector<std::string>& order() const { return order_; }

    /// Checks whether every parameter could be worked out.
    /// @return True if none has a problem.
    bool ok() const;

    /// Gives what other formulas (like a sketch's dimensions) use to look
    /// parameters up. A parameter that has a problem counts as not there.
    /// The lookup refers to this object, so keep it alive while using it.
    /// @return The lookup.
    NameLookup lookup() const;

private:
    friend class ParameterTable;
    std::map<std::string, ParameterValue, std::less<>> values_;
    std::vector<std::string> order_;
};

/// A design's parameters (see Document::parameters()). Change them only with
/// SetParameterCommand, RenameParameterCommand and DeleteParameterCommand.
///
/// @code
/// ParameterTable table;
/// table.define({ParamId::generate(), "wall", "3 mm", ""});
/// table.define({ParamId::generate(), "height", "wall * 4", ""});
/// auto values = table.evaluate();
/// values.find("height")->value->value;  // 12 (mm)
/// table.dependents("wall");             // {"height"}
/// @endcode
class ParameterTable {
public:
    /// Lists the parameters, in the order they were added.
    /// @return The parameters.
    const std::vector<Parameter>& parameters() const { return list_; }

    /// Checks whether there are any parameters.
    /// @return True if there are none.
    bool empty() const { return list_.empty(); }

    /// Finds a parameter by name.
    /// @param name Its name; upper and lower case must match.
    /// @return It, or nullptr if there's none by that name.
    const Parameter* find(std::string_view name) const;

    /// Finds a parameter by name, ignoring upper and lower case, to catch a
    /// name that's the same but for case.
    /// @param name The name.
    /// @return It, or nullptr if there's none by that name.
    const Parameter* findIgnoringCase(std::string_view name) const;

    /// Finds a parameter by id.
    /// @param id Its id.
    /// @return It, or nullptr if there's none with that id.
    const Parameter* find(const ParamId& id) const;

    /// Adds a parameter, or changes the one with the same id. It isn't checked:
    /// commands check first.
    /// @param parameter The parameter.
    void define(Parameter parameter);

    /// Puts a parameter back where it was (used by undo).
    /// @param index Its place; past the end means at the end.
    /// @param parameter The parameter.
    void insert(std::size_t index, Parameter parameter);

    /// Takes a parameter out.
    /// @param id Its id.
    /// @return Where it was, or nothing if no parameter has that id.
    std::optional<std::size_t> remove(const ParamId& id);

    /// Works every parameter out, each after the ones it uses. A parameter that
    /// uses a name that isn't there, uses itself (or one that uses it back), or
    /// has units that don't fit gets its problem instead of a value; so does
    /// every parameter that uses it.
    /// @return The values.
    ParameterValues evaluate() const;

    /// Lists the parameters whose formulas use a name.
    /// @param name The name used.
    /// @return Their names, in table order.
    std::vector<std::string> dependents(std::string_view name) const;

    /// Checks that a name can be a parameter's: it starts with a letter, has only
    /// letters, digits and _, is at most 40 characters, and isn't a word formulas
    /// already use (a unit like mm, a function like sin, or pi).
    /// @param name The name.
    /// @return Why it can't be, in plain words; empty if it can.
    static Diagnostics checkName(std::string_view name);

private:
    std::vector<Parameter> list_;
};

}  // namespace kh::model
