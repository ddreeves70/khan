#pragma once

/// @file
/// The units a design shows its lengths in. Inside, Khan always works in
/// millimeters; these only change what the student sees and types.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kh {

/// A unit for showing and typing lengths. Each design has one (its Document
/// settings); millimeters unless the student picks another.
///
/// @code
/// const double mm = 25.4;
/// kh::lengthText(mm, kh::LengthUnit::Inch);       // "1 in"
/// kh::toUnit(mm, kh::LengthUnit::Centimeter);     // 2.54
/// kh::fromUnit(2, kh::LengthUnit::Foot);          // 609.6 (mm)
/// @endcode
enum class LengthUnit {
    Millimeter,  ///< mm, the usual unit for 3D printing. 1 mm.
    Centimeter,  ///< cm. 10 mm.
    Meter,       ///< m. 1000 mm.
    Inch,        ///< in. 25.4 mm.
    Foot,        ///< ft. 304.8 mm.
};

/// Gives every length unit, in the order Document settings lists them.
/// @return Millimeter, Centimeter, Meter, Inch, Foot.
const std::vector<LengthUnit>& allLengthUnits();

/// Gives how many millimeters one of a unit is.
/// @param unit The unit.
/// @return 1, 10, 1000, 25.4 or 304.8 (mm).
double mmPerUnit(LengthUnit unit);

/// Turns millimeters into a unit.
/// @param mm A length, in mm.
/// @param unit The unit to give it in.
/// @return The length in @p unit.
double toUnit(double mm, LengthUnit unit);

/// Turns a length in a unit into millimeters.
/// @param value A length, in @p unit.
/// @param unit Its unit.
/// @return The length in mm.
double fromUnit(double value, LengthUnit unit);

/// Gives a unit's short name, as shown after numbers and saved in files.
/// @param unit The unit.
/// @return "mm", "cm", "m", "in" or "ft".
std::string_view unitSymbol(LengthUnit unit);

/// Gives a unit's name in words, for menus.
/// @param unit The unit.
/// @return "Millimeters", "Centimeters", "Meters", "Inches" or "Feet".
std::string_view unitName(LengthUnit unit);

/// Finds a unit by its short name (see unitSymbol()), ignoring upper and lower case.
/// @param symbol For example "in".
/// @return The unit, or nothing if no unit has that short name.
std::optional<LengthUnit> parseLengthUnit(std::string_view symbol);

/// Gives how many decimals to show for a unit, so a tenth of a millimeter or
/// so still shows: 2 for mm, 3 for cm and in, 4 for m and ft.
/// @param unit The unit.
/// @return The number of decimals.
int unitDecimals(LengthUnit unit);

/// Writes a length for the student, in a unit, with no extra zeros: "40 mm",
/// "1.575 in". It's rounded to unitDecimals() decimals.
/// @param mm The length, in mm.
/// @param unit The unit to write it in.
/// @return The number and the unit's short name, with a space between.
std::string lengthText(double mm, LengthUnit unit);

/// Writes a volume for the student, in a unit cubed, with 2 decimals:
/// "3748.67 mm^3", "0.23 in^3".
/// @param mm3 The volume, in mm^3.
/// @param unit The length unit; the volume is given in it cubed.
/// @return The number and the unit's short name with "^3".
std::string volumeText(double mm3, LengthUnit unit);

}  // namespace kh
