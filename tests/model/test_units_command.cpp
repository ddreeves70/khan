// Tests for the design's units: the document.set_units command, and undo.

#include <catch2/catch_test_macros.hpp>

#include "sb/model/CommandRegistry.h"
#include "sb/model/Commands.h"
#include "sb/model/Document.h"

using namespace sb;
using namespace sb::model;

TEST_CASE("document.set_units changes the unit lengths are shown in, and undoes", "[model][units]") {
    Document doc;
    CHECK(doc.lengthUnit() == LengthUnit::Millimeter);  // the usual unit
    auto made = CommandRegistry::builtin().create("document.set_units", {{"unit", std::string("in")}}, doc);
    REQUIRE(made.ok());
    REQUIRE(doc.execute(std::move(made.command)).empty());
    CHECK(doc.lengthUnit() == LengthUnit::Inch);
    CHECK(doc.undoLabel() == "Use inches");
    REQUIRE(doc.execute(std::make_unique<SetUnitsCommand>(LengthUnit::Centimeter)).empty());
    CHECK(doc.lengthUnit() == LengthUnit::Centimeter);
    doc.undo();
    CHECK(doc.lengthUnit() == LengthUnit::Inch);
    doc.undo();
    CHECK(doc.lengthUnit() == LengthUnit::Millimeter);
    doc.redo();
    CHECK(doc.lengthUnit() == LengthUnit::Inch);
}

TEST_CASE("document.set_units only takes units SketchBillet knows", "[model][units]") {
    Document doc;
    auto made =
        CommandRegistry::builtin().create("document.set_units", {{"unit", std::string("cubits")}}, doc);
    CHECK_FALSE(made.ok());
    CHECK(doc.lengthUnit() == LengthUnit::Millimeter);
}
