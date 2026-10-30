// Tests for the units a design shows lengths in (kh/core/Units.h).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "kh/core/Units.h"

using namespace kh;
using Catch::Approx;

TEST_CASE("length units turn into and out of millimeters", "[core][units]") {
    CHECK(allLengthUnits().size() == 5);
    CHECK(allLengthUnits().front() == LengthUnit::Millimeter);  // the usual one first
    CHECK(mmPerUnit(LengthUnit::Inch) == 25.4);
    CHECK(mmPerUnit(LengthUnit::Foot) == Approx(12 * 25.4));
    CHECK(toUnit(25.4, LengthUnit::Inch) == Approx(1));
    CHECK(toUnit(1500, LengthUnit::Meter) == Approx(1.5));
    CHECK(fromUnit(2, LengthUnit::Foot) == Approx(609.6));
    for (auto u : allLengthUnits()) {
        INFO(unitSymbol(u));
        CHECK(fromUnit(toUnit(123.456, u), u) == Approx(123.456));  // there and back
        CHECK(parseLengthUnit(unitSymbol(u)) == u);
        CHECK_FALSE(unitName(u).empty());
    }
    CHECK(parseLengthUnit("IN") == LengthUnit::Inch);
    CHECK_FALSE(parseLengthUnit("yards"));
    CHECK_FALSE(parseLengthUnit(""));
}

TEST_CASE("lengths and volumes are written for the student, with no extra zeros", "[core][units]") {
    CHECK(lengthText(40, LengthUnit::Millimeter) == "40 mm");
    CHECK(lengthText(40.126, LengthUnit::Millimeter) == "40.13 mm");  // 2 decimals for mm
    CHECK(lengthText(25.4, LengthUnit::Inch) == "1 in");
    CHECK(lengthText(40, LengthUnit::Inch) == "1.575 in");  // 3 decimals for inches
    CHECK(lengthText(5, LengthUnit::Centimeter) == "0.5 cm");
    CHECK(lengthText(-0.0001, LengthUnit::Millimeter) == "0 mm");  // never "-0"
    CHECK(lengthText(1234, LengthUnit::Meter) == "1.234 m");
    CHECK(volumeText(3748.6726, LengthUnit::Millimeter) == "3748.67 mm^3");
    CHECK(volumeText(1000, LengthUnit::Centimeter) == "1.00 cm^3");
    CHECK(volumeText(25.4 * 25.4 * 25.4 * 2, LengthUnit::Inch) == "2.00 in^3");
}
