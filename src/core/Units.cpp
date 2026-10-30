#include "kh/core/Units.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace kh {

const std::vector<LengthUnit>& allLengthUnits() {
    static const std::vector<LengthUnit> units{LengthUnit::Millimeter, LengthUnit::Centimeter,
                                               LengthUnit::Meter, LengthUnit::Inch, LengthUnit::Foot};
    return units;
}

double mmPerUnit(LengthUnit unit) {
    switch (unit) {
        case LengthUnit::Millimeter:
            return 1.0;
        case LengthUnit::Centimeter:
            return 10.0;
        case LengthUnit::Meter:
            return 1000.0;
        case LengthUnit::Inch:
            return 25.4;
        case LengthUnit::Foot:
            return 304.8;
    }
    return 1.0;
}

double toUnit(double mm, LengthUnit unit) {
    return mm / mmPerUnit(unit);
}

double fromUnit(double value, LengthUnit unit) {
    return value * mmPerUnit(unit);
}

std::string_view unitSymbol(LengthUnit unit) {
    switch (unit) {
        case LengthUnit::Millimeter:
            return "mm";
        case LengthUnit::Centimeter:
            return "cm";
        case LengthUnit::Meter:
            return "m";
        case LengthUnit::Inch:
            return "in";
        case LengthUnit::Foot:
            return "ft";
    }
    return "mm";
}

std::string_view unitName(LengthUnit unit) {
    switch (unit) {
        case LengthUnit::Millimeter:
            return "Millimeters";
        case LengthUnit::Centimeter:
            return "Centimeters";
        case LengthUnit::Meter:
            return "Meters";
        case LengthUnit::Inch:
            return "Inches";
        case LengthUnit::Foot:
            return "Feet";
    }
    return "Millimeters";
}

std::optional<LengthUnit> parseLengthUnit(std::string_view symbol) {
    std::string lower(symbol);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (auto u : allLengthUnits()) {
        if (unitSymbol(u) == lower) return u;
    }
    return std::nullopt;
}

int unitDecimals(LengthUnit unit) {
    switch (unit) {
        case LengthUnit::Millimeter:
            return 2;
        case LengthUnit::Centimeter:
        case LengthUnit::Inch:
            return 3;
        case LengthUnit::Meter:
        case LengthUnit::Foot:
            return 4;
    }
    return 2;
}

namespace {

// A number with a fixed number of decimals, then the zeros at the end dropped
// (and the point, if nothing's after it). Never "-0".
std::string trimmed(double value, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, value);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

}  // namespace

std::string lengthText(double mm, LengthUnit unit) {
    return trimmed(toUnit(mm, unit), unitDecimals(unit)) + " " + std::string(unitSymbol(unit));
}

std::string volumeText(double mm3, LengthUnit unit) {
    const double per = mmPerUnit(unit);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.2f", mm3 / (per * per * per));
    return std::string(buf) + " " + std::string(unitSymbol(unit)) + "^3";
}

}  // namespace kh
