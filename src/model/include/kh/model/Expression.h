#pragma once

/// @file
/// Formulas such as `wall * 2 + 1 mm`: reading them, checking their units and
/// working them out.

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kh/core/Diagnostic.h"
#include "kh/core/Units.h"

namespace kh::model {

/// What a value measures, as powers of length and angle: a length is
/// {1, 0}, an area {2, 0}, an angle {0, 1} and a plain number {0, 0}.
struct Dimension {
    int length = 0;  ///< The power of length: 1 for mm, 2 for mm^2, -1 for "per mm".
    int angle = 0;   ///< The power of angle: 1 for an angle.

    /// Compares two dimensions.
    /// @param other The other dimension.
    /// @return True if both powers are the same.
    bool operator==(const Dimension& other) const = default;
};

/// A plain number: no length, no angle.
inline constexpr Dimension kPlainNumber{0, 0};
/// A length, such as 3 mm.
inline constexpr Dimension kLength{1, 0};
/// An angle, such as 90 deg.
inline constexpr Dimension kAngle{0, 1};

/// Names what a dimension measures, for messages: "plain number", "length",
/// "area", "volume", "angle", or something like "length^4".
/// @param dim The dimension.
/// @return Its name.
std::string dimensionName(const Dimension& dim);

/// A number with what it measures. Lengths are in mm and angles in radians,
/// like everywhere inside Khan, so 1 in is {25.4, kLength}.
struct Quantity {
    double value = 0;  ///< The number, in mm^length and radians^angle.
    Dimension dim;     ///< What it measures.
};

/// Writes a value for the student, in the design's unit: "3 mm", "1.5 in",
/// "90 deg", "12 mm^2", "4".
/// @param q The value.
/// @param unit The unit lengths are shown in.
/// @return The number and its unit.
std::string quantityText(const Quantity& q, LengthUnit unit);

/// Finds the value of a name in a formula.
/// @param name The name, as written in the formula.
/// @return Its value, or nothing if there's nothing by that name.
using NameLookup = std::function<std::optional<Quantity>(std::string_view name)>;

/// The result of working out an Expression.
struct EvalResult {
    std::optional<Quantity> value;  ///< The value, if it could be worked out.
    Diagnostics issues;             ///< Why not, in plain words; empty if it worked.

    /// Checks whether it worked.
    /// @return True if there's a value.
    bool ok() const { return value.has_value(); }
};

/// A formula a student typed, such as `wall * 2 + 1 mm` or `30 deg`.
///
/// It may use numbers with units (mm, cm, m, in, ft, deg, rad, or °), names,
/// `+ - * / ^`, brackets, `pi`, and the functions `sqrt`, `abs`, `min`,
/// `max`, `sin`, `cos`, `tan`, `asin`, `acos` and `atan`. A number without a
/// unit is a plain number. Units are checked: lengths add to lengths, and
/// `sin` takes an angle.
///
/// @code
/// auto e = Expression::parse("wall * 2 + 1 mm");
/// e.references();  // {"wall"}
/// auto r = e.eval([](std::string_view) { return Quantity{3, kLength}; });
/// r.value->value;  // 7 (mm)
/// @endcode
class Expression {
public:
    /// Makes an empty expression, which doesn't parse.
    Expression();

    /// Reads a formula. Check ok() afterwards.
    /// @param text The formula, as typed.
    /// @return The expression; problems() says what's wrong if it couldn't be read.
    static Expression parse(std::string_view text);

    /// Checks whether the formula could be read.
    /// @return True if it could.
    bool ok() const { return problems_.empty(); }

    /// Says why the formula couldn't be read.
    /// @return The problems, in plain words; empty if it was read.
    const Diagnostics& problems() const { return problems_; }

    /// Gives the formula as it was typed.
    /// @return The text.
    const std::string& text() const { return text_; }

    /// Lists the names the formula uses, each once, in the order they first appear.
    /// Function names and `pi` aren't listed.
    /// @return The names.
    std::vector<std::string> references() const;

    /// Works the formula out.
    /// @param lookup Gives the value of each name it uses.
    /// @return The value, or why it can't be worked out (a name that isn't
    ///         there, units that don't fit, dividing by zero).
    EvalResult eval(const NameLookup& lookup) const;

    /// Checks whether the formula is just a number, like `3 mm`, with no names.
    /// @return True if it uses no names.
    bool isConstant() const { return ok() && names_.empty(); }

    /// Gives the formula with one name changed, everything else as typed.
    /// @param from The old name.
    /// @param to The new name.
    /// @return The new text.
    std::string renamed(std::string_view from, std::string_view to) const;

    /// Checks whether a word is one formulas already use: a unit, a function or `pi`.
    /// @param word For example "mm" or "sin".
    /// @return True if it is, so it can't be a parameter's name.
    static bool isReservedWord(std::string_view word);

    /// One part of the formula, after reading (inside Expression.cpp).
    struct Node;

private:
    struct NameAt {
        std::string name;
        std::size_t at = 0;  // where it starts in text_
    };
    std::string text_;
    std::shared_ptr<const Node> root_;  // shared: an Expression is a value, and nodes never change
    std::vector<NameAt> names_;
    Diagnostics problems_;
};

}  // namespace kh::model
