// Tests for formulas (Expression), the parameter table and the parameter commands.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <map>
#include <numbers>
#include <string>

#include "kh/model/CommandRegistry.h"
#include "kh/model/Commands.h"
#include "kh/model/Document.h"
#include "kh/model/Expression.h"
#include "kh/model/Parameters.h"

using namespace kh;
using namespace kh::model;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kDeg = std::numbers::pi / 180;

// Works out a formula whose names come from a small map.
EvalResult evalWith(std::string_view text, const std::map<std::string, Quantity, std::less<>>& names = {}) {
    return Expression::parse(text).eval([&](std::string_view name) -> std::optional<Quantity> {
        const auto it = names.find(name);
        return it == names.end() ? std::nullopt : std::optional<Quantity>(it->second);
    });
}

// The value of a formula that should work; fails the test if it doesn't.
Quantity valueOf(std::string_view text, const std::map<std::string, Quantity, std::less<>>& names = {}) {
    auto r = evalWith(text, names);
    INFO(text << ": " << (r.issues.empty() ? std::string() : r.issues.front().message));
    REQUIRE(r.ok());
    return *r.value;
}

// The code of the first problem with a formula that shouldn't work.
std::string problemOf(std::string_view text, const std::map<std::string, Quantity, std::less<>>& names = {}) {
    auto r = evalWith(text, names);
    REQUIRE_FALSE(r.ok());
    REQUIRE_FALSE(r.issues.empty());
    return r.issues.front().code;
}

Diagnostics run(Document& doc, std::unique_ptr<Command> command) {
    return doc.execute(std::move(command));
}

}  // namespace

// ---------------------------------------------------------------------------
// Expression
// ---------------------------------------------------------------------------

TEST_CASE("formulas follow the usual order: brackets, powers, times and divide, then plus and minus",
          "[model][expression]") {
    CHECK(valueOf("1 + 2 * 3").value == 7);
    CHECK(valueOf("(1 + 2) * 3").value == 9);
    CHECK(valueOf("2 ^ 3 ^ 2").value == 512);  // 2 ^ 9
    CHECK(valueOf("-2 ^ 2").value == -4);      // minus the square, as in maths
    CHECK(valueOf("2 ^ -1").value == 0.5);
    CHECK(valueOf("10 - 4 - 3").value == 3);  // left to right
    CHECK(valueOf("12 / 4 / 3").value == 1);
    CHECK(valueOf(".5 + 1e1").value == 10.5);
    CHECK_THAT(valueOf("pi").value, WithinAbs(std::numbers::pi, 1e-15));
    CHECK(valueOf("  4  ").dim == kPlainNumber);
}

TEST_CASE("numbers with units become mm and radians", "[model][expression]") {
    CHECK(valueOf("3 mm").value == 3);
    CHECK(valueOf("3mm").value == 3);
    CHECK(valueOf("2 cm").value == 20);
    CHECK(valueOf("1.5 m").value == 1500);
    CHECK(valueOf("1 in").value == 25.4);
    CHECK(valueOf("2 ft").value == 609.6);
    CHECK(valueOf("1 in").dim == kLength);
    CHECK_THAT(valueOf("90 deg").value, WithinAbs(std::numbers::pi / 2, 1e-15));
    CHECK_THAT(valueOf("90\xC2\xB0").value, WithinAbs(std::numbers::pi / 2, 1e-15));  // 90°
    CHECK(valueOf("2 rad").value == 2);
    CHECK(valueOf("30 deg").dim == kAngle);
    CHECK(valueOf("1 in + 1 mm").value == 26.4);  // any length adds to any length
}

TEST_CASE("units multiply and divide", "[model][expression]") {
    CHECK(valueOf("2 mm * 3 mm").dim == Dimension{2, 0});  // an area
    CHECK(valueOf("2 mm * 3 mm * 4 mm").value == 24);
    CHECK(valueOf("6 mm / 2 mm").dim == kPlainNumber);  // a ratio
    CHECK(valueOf("6 mm / 2").dim == kLength);
    CHECK(valueOf("(3 mm) ^ 2").dim == Dimension{2, 0});
    CHECK(valueOf("sqrt(16 mm * 4 mm)").value == 8);
    CHECK(valueOf("sqrt(16 mm * 4 mm)").dim == kLength);
    CHECK(valueOf("1 / (2 mm)").dim == Dimension{-1, 0});
}

TEST_CASE("functions work out the right values and units", "[model][expression]") {
    CHECK_THAT(valueOf("sin(30 deg)").value, WithinAbs(0.5, 1e-12));
    CHECK_THAT(valueOf("cos(60 deg)").value, WithinAbs(0.5, 1e-12));
    CHECK_THAT(valueOf("tan(45 deg)").value, WithinAbs(1, 1e-12));
    CHECK_THAT(valueOf("asin(0.5)").value, WithinAbs(30 * kDeg, 1e-12));
    CHECK(valueOf("acos(0.5)").dim == kAngle);
    CHECK_THAT(valueOf("atan(1)").value, WithinAbs(45 * kDeg, 1e-12));
    CHECK(valueOf("abs(-3 mm)").value == 3);
    CHECK(valueOf("min(3 mm, 1 in, 5 mm)").value == 3);
    CHECK(valueOf("max(3 mm, 1 in, 5 mm)").value == 25.4);
    // A property: sin² + cos² is 1 for any angle.
    for (int d = -360; d <= 360; d += 15) {
        const auto a = std::to_string(d) + " deg";
        CHECK_THAT(valueOf("sin(" + a + ") ^ 2 + cos(" + a + ") ^ 2").value, WithinAbs(1, 1e-12));
    }
}

TEST_CASE("names in formulas are looked up, and listed once each", "[model][expression]") {
    const std::map<std::string, Quantity, std::less<>> names{{"wall", {3, kLength}},
                                                             {"count", {4, kPlainNumber}}};
    CHECK(valueOf("wall * count + 1 mm", names).value == 13);
    const auto e = Expression::parse("wall * 2 + wall / count + Sketch1.width");
    REQUIRE(e.ok());
    CHECK(e.references() == std::vector<std::string>{"wall", "count", "Sketch1.width"});
    CHECK_FALSE(e.isConstant());
    CHECK(Expression::parse("3 mm * pi").isConstant());
    CHECK(Expression::parse("sqrt(2)").references().empty());  // functions aren't names
    CHECK(problemOf("wal * 2", names) == "expression.unknown_name");
}

TEST_CASE("renaming changes the name and nothing else", "[model][expression]") {
    const auto e = Expression::parse("wall*2 + (wall_2 - wall) / walls");
    CHECK(e.renamed("wall", "t") == "t*2 + (wall_2 - t) / walls");
    CHECK(e.renamed("nothing", "t") == e.text());
}

TEST_CASE("formulas that can't be read say why", "[model][expression]") {
    CHECK(problemOf("") == "expression.empty");
    CHECK(problemOf("   ") == "expression.empty");
    CHECK(problemOf("3 $") == "expression.character");
    CHECK(problemOf("3 \xC3\x97 2") == "expression.character");  // ×
    CHECK(problemOf("(3 + 2") == "expression.bracket");
    CHECK(problemOf("3 + 2)") == "expression.bracket");
    CHECK(problemOf("3 +") == "expression.incomplete");
    CHECK(problemOf("3 * * 2") == "expression.operator");
    CHECK(problemOf("3 4") == "expression.operator");
    CHECK(problemOf("2 kg") == "expression.unit");
    CHECK(problemOf("mm") == "expression.unit");
    CHECK(problemOf("sin 30 deg") == "expression.function");
    CHECK(problemOf("sin(30 deg, 2 deg)") == "expression.function");
    CHECK(problemOf("max(3 mm)") == "expression.function");
    CHECK(problemOf("blah(3)") == "expression.function");

    auto e = Expression::parse("2 kg");
    REQUIRE_FALSE(e.ok());
    CHECK_THAT(e.problems().front().message, ContainsSubstring("put * between 2 and kg"));
    CHECK_FALSE(Expression().ok());  // an empty expression has nothing to work out
}

TEST_CASE("units that don't fit are refused, with the fix", "[model][expression]") {
    const std::map<std::string, Quantity, std::less<>> names{{"wall", {3, kLength}}};
    CHECK(problemOf("wall + 2", names) == "expression.units");
    auto r = evalWith("wall + 2", names);
    CHECK(r.issues.front().message ==
          "Khan can't add 2 to wall: wall is a length and 2 is a plain number. Give both the same kind of "
          "unit, "
          "like 2 mm.");
    CHECK(problemOf("3 mm - 30 deg") == "expression.units");
    CHECK(problemOf("sin(30)") == "expression.units");
    CHECK(problemOf("asin(1 mm)") == "expression.units");
    CHECK(problemOf("sqrt(4 mm)") == "expression.units");
    CHECK(problemOf("2 ^ (1 mm)") == "expression.units");
    CHECK(problemOf("(2 mm) ^ 0.5") == "expression.units");
    CHECK(problemOf("min(1 mm, 2 deg)") == "expression.units");
}

TEST_CASE("sums with no answer are refused", "[model][expression]") {
    CHECK(problemOf("1 mm / 0") == "expression.divide_by_zero");
    CHECK(problemOf("1 / (2 - 2)") == "expression.divide_by_zero");
    CHECK(problemOf("0 ^ -1") == "expression.divide_by_zero");
    CHECK(problemOf("sqrt(-4)") == "expression.negative");
    CHECK(problemOf("(-8) ^ 0.5") == "expression.negative");
    CHECK(problemOf("asin(2)") == "expression.range");
    CHECK(problemOf("10 ^ 400") == "expression.too_big");
}

TEST_CASE("values are written in the design's unit", "[model][expression]") {
    CHECK(quantityText({3, kLength}, LengthUnit::Millimeter) == "3 mm");
    CHECK(quantityText({25.4, kLength}, LengthUnit::Inch) == "1 in");
    CHECK(quantityText({std::numbers::pi / 2, kAngle}, LengthUnit::Millimeter) == "90 deg");
    CHECK(quantityText({4, kPlainNumber}, LengthUnit::Millimeter) == "4");
    CHECK(quantityText({0.25, kPlainNumber}, LengthUnit::Millimeter) == "0.25");
    CHECK(quantityText({12, {2, 0}}, LengthUnit::Millimeter) == "12 mm^2");
    CHECK(quantityText({100, {2, 0}}, LengthUnit::Centimeter) == "1 cm^2");
    CHECK(quantityText({2, {-1, 0}}, LengthUnit::Millimeter) == "2 1/mm");
    CHECK(dimensionName(kLength) == "length");
    CHECK(dimensionName({3, 0}) == "volume");
    CHECK(dimensionName({4, 0}) == "length^4");
}

// ---------------------------------------------------------------------------
// ParameterTable
// ---------------------------------------------------------------------------

TEST_CASE("parameters are worked out after the ones they use, whatever order they're in",
          "[model][parameters]") {
    ParameterTable table;
    table.define({ParamId::generate(), "height", "wall * 4", ""});
    table.define({ParamId::generate(), "depth", "height + wall", ""});
    table.define({ParamId::generate(), "wall", "3 mm", ""});
    const auto values = table.evaluate();
    CHECK(values.ok());
    CHECK(values.find("height")->value->value == 12);
    CHECK(values.find("depth")->value->value == 15);
    CHECK(values.order() == std::vector<std::string>{"wall", "height", "depth"});
    CHECK(table.dependents("wall") == std::vector<std::string>{"height", "depth"});
    CHECK(table.dependents("depth").empty());
    CHECK(values.find("nothing") == nullptr);

    // Other formulas, like a sketch's dimensions, can use them.
    CHECK(Expression::parse("depth * 2").eval(values.lookup()).value->value == 30);
}

TEST_CASE("a parameter that uses itself, or a loop of them, gets a plain problem", "[model][parameters]") {
    ParameterTable table;
    table.define({ParamId::generate(), "a", "b + 1 mm", ""});
    table.define({ParamId::generate(), "b", "a * 2", ""});
    table.define({ParamId::generate(), "c", "a + 1 mm", ""});  // uses the loop
    table.define({ParamId::generate(), "d", "d", ""});         // uses itself
    table.define({ParamId::generate(), "e", "5 mm", ""});      // fine
    const auto values = table.evaluate();
    CHECK_FALSE(values.ok());
    REQUIRE_FALSE(values.find("a")->ok());
    CHECK(values.find("a")->issues.front().code == "parameter.loop");
    CHECK(values.find("a")->issues.front().message ==
          "a uses b and b uses a, so Khan can't work either out. Change one so it doesn't use the other.");
    CHECK(values.find("b")->issues.front().code == "parameter.loop");
    CHECK(values.find("c")->issues.front().code == "parameter.uses_problem");
    CHECK(values.find("d")->issues.front().message ==
          "d uses itself, so Khan can't work it out. Give it a value that doesn't use d, like 3 mm.");
    CHECK(values.find("e")->value->value == 5);
    CHECK(values.order().size() == 5);
}

TEST_CASE("a name that's wrong only in upper and lower case gets a hint", "[model][parameters]") {
    ParameterTable table;
    table.define({ParamId::generate(), "wall", "3 mm", ""});
    table.define({ParamId::generate(), "height", "Wall * 4", ""});
    const auto values = table.evaluate();
    CHECK_THAT(values.find("height")->issues.front().message,
               ContainsSubstring("Did you mean wall? Upper and lower case must match."));
}

TEST_CASE("parameter names are checked", "[model][parameters]") {
    CHECK(ParameterTable::checkName("wall").empty());
    CHECK(ParameterTable::checkName("wall_2").empty());
    CHECK(ParameterTable::checkName("W").empty());
    CHECK_FALSE(ParameterTable::checkName("").empty());
    CHECK_FALSE(ParameterTable::checkName("2wall").empty());
    CHECK_FALSE(ParameterTable::checkName("_wall").empty());
    CHECK_FALSE(ParameterTable::checkName("wall thickness").empty());
    CHECK_FALSE(ParameterTable::checkName("Sketch1.width").empty());  // dots are for steps' values
    CHECK_FALSE(ParameterTable::checkName(std::string(41, 'a')).empty());
    for (const char* word : {"mm", "in", "m", "deg", "pi", "sin", "sqrt", "max"}) {
        INFO(word);
        CHECK(ParameterTable::checkName(word).front().code == "parameter.name");
    }
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

TEST_CASE("parameter.set adds and changes a parameter, and undoes", "[model][parameters]") {
    Document doc;
    const auto& registry = CommandRegistry::builtin();
    auto made = registry.create("parameter.set",
                                {{"name", std::string("wall")},
                                 {"value", std::string("3 mm")},
                                 {"note", std::string("How thick the walls are")}},
                                doc);
    REQUIRE(made.ok());
    REQUIRE(run(doc, std::move(made.command)).empty());
    REQUIRE(doc.parameters().find("wall"));
    const auto id = doc.parameters().find("wall")->id;
    CHECK(doc.undoLabel() == "Add parameter wall");
    CHECK(doc.parameters().find("wall")->note == "How thick the walls are");

    REQUIRE(run(doc, std::make_unique<SetParameterCommand>("wall", "4 mm")).empty());
    CHECK(doc.undoLabel() == "Change wall to 4 mm");
    CHECK(doc.parameters().find("wall")->expression == "4 mm");
    CHECK(doc.parameters().find("wall")->note == "How thick the walls are");  // kept
    CHECK(doc.parameters().find("wall")->id == id);                           // same parameter

    doc.undo();
    CHECK(doc.parameters().find("wall")->expression == "3 mm");
    doc.undo();
    CHECK(doc.parameters().empty());
    doc.redo();
    REQUIRE(doc.parameters().find("wall"));
    CHECK(doc.parameters().find("wall")->id == id);  // the same id on redo
    CHECK(doc.isModified());
}

TEST_CASE("parameter.set refuses what can't be worked out, and changes nothing", "[model][parameters]") {
    Document doc;
    REQUIRE(run(doc, std::make_unique<SetParameterCommand>("wall", "3 mm")).empty());
    REQUIRE(run(doc, std::make_unique<SetParameterCommand>("height", "wall * 4 + 1 mm")).empty());

    auto first = [&](std::string name, std::string value) {
        auto issues = run(doc, std::make_unique<SetParameterCommand>(std::move(name), std::move(value)));
        REQUIRE_FALSE(issues.empty());
        return issues.front();
    };
    CHECK(first("2wall", "3 mm").code == "parameter.name");
    CHECK(first("depth", "3 +").code == "expression.incomplete");
    CHECK(first("depth", "walll * 2").code == "expression.unknown_name");
    CHECK(first("depth", "wall + 2").code == "expression.units");
    CHECK(first("Wall", "5 mm").code == "parameter.name_taken");
    CHECK(first("wall", "height / 4").code == "parameter.loop");
    const auto broke = first("wall", "3");  // height would add a plain number to 1 mm
    CHECK(broke.code == "parameter.breaks");
    CHECK_THAT(broke.message, StartsWith("Changing wall to 3 would stop height working: "));

    CHECK(doc.parameters().parameters().size() == 2);
    CHECK(doc.parameters().find("wall")->expression == "3 mm");
    CHECK(doc.undoLabel() == "Add parameter height");
}

TEST_CASE("a parameter can be added before the one it uses is fixed", "[model][parameters]") {
    // Opening a file can leave a parameter with a problem; adding the missing
    // one fixes it, and isn't refused for "changing" it.
    Document doc;
    REQUIRE(run(doc, std::make_unique<ReplaceParametersCommand>(
                         std::vector<Parameter>{{ParamId::generate(), "height", "wall * 4", ""}}))
                .empty());
    CHECK_FALSE(doc.parameters().evaluate().ok());
    REQUIRE(run(doc, std::make_unique<SetParameterCommand>("wall", "3 mm")).empty());
    CHECK(doc.parameters().evaluate().find("height")->value->value == 12);
}

TEST_CASE("parameter.rename renames every use too, and undoes", "[model][parameters]") {
    Document doc;
    REQUIRE(run(doc, std::make_unique<SetParameterCommand>("wall", "3 mm")).empty());
    REQUIRE(run(doc, std::make_unique<SetParameterCommand>("height", "wall * 4 + wall")).empty());
    REQUIRE(run(doc, std::make_unique<SetParameterCommand>("width", "20 mm")).empty());
    const auto id = doc.parameters().find("wall")->id;

    auto made = CommandRegistry::builtin().create(
        "parameter.rename", {{"name", std::string("wall")}, {"to", std::string("thickness")}}, doc);
    REQUIRE(made.ok());
    REQUIRE(run(doc, std::move(made.command)).empty());
    CHECK(doc.undoLabel() == "Rename wall to thickness");
    CHECK(doc.parameters().find("wall") == nullptr);
    CHECK(doc.parameters().find("thickness")->id == id);
    CHECK(doc.parameters().find("height")->expression == "thickness * 4 + thickness");
    CHECK(doc.parameters().evaluate().find("height")->value->value == 15);

    doc.undo();
    CHECK(doc.parameters().find("wall")->id == id);
    CHECK(doc.parameters().find("height")->expression == "wall * 4 + wall");
    CHECK(doc.parameters().parameters()[0].name == "wall");  // still first

    auto refused = [&](std::string from, std::string to) {
        auto issues = run(doc, std::make_unique<RenameParameterCommand>(std::move(from), std::move(to)));
        REQUIRE_FALSE(issues.empty());
        return issues.front().code;
    };
    CHECK(refused("wal", "x") == "parameter.unknown");
    CHECK(refused("wall", "Width") == "parameter.name_taken");
    CHECK(refused("wall", "sin") == "parameter.name");
    CHECK(refused("wall", "wall") == "parameter.same_name");
    REQUIRE(run(doc, std::make_unique<RenameParameterCommand>("wall", "Wall")).empty());  // only case changes
    CHECK(doc.parameters().find("height")->expression == "Wall * 4 + Wall");
}

TEST_CASE("parameter.delete removes an unused parameter, and undo puts it back in its place",
          "[model][parameters]") {
    Document doc;
    REQUIRE(run(doc, std::make_unique<SetParameterCommand>("wall", "3 mm")).empty());
    REQUIRE(run(doc, std::make_unique<SetParameterCommand>("height", "wall * 4")).empty());
    REQUIRE(run(doc, std::make_unique<SetParameterCommand>("width", "wall * 2")).empty());
    REQUIRE(run(doc, std::make_unique<SetParameterCommand>("gap", "1 mm")).empty());

    auto issues = run(doc, std::make_unique<DeleteParameterCommand>("wall"));
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().code == "parameter.in_use");
    CHECK(issues.front().message ==
          "height and width use wall, so wall can't be deleted. Change them so they don't use wall first, or "
          "delete them too.");
    CHECK(run(doc, std::make_unique<DeleteParameterCommand>("nope")).front().code == "parameter.unknown");

    auto made = CommandRegistry::builtin().create("parameter.delete", {{"name", std::string("height")}}, doc);
    REQUIRE(made.ok());
    REQUIRE(run(doc, std::move(made.command)).empty());
    CHECK(doc.undoLabel() == "Delete parameter height");
    CHECK(doc.parameters().find("height") == nullptr);
    issues = run(doc, std::make_unique<DeleteParameterCommand>("wall"));
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().message ==
          "width uses wall, so wall can't be deleted. Change width so it doesn't use wall first, or delete "
          "width "
          "too.");

    doc.undo();
    REQUIRE(doc.parameters().parameters().size() == 4);
    CHECK(doc.parameters().parameters()[1].name == "height");  // back in its place
}

TEST_CASE("replacing all parameters checks names but keeps formulas with problems", "[model][parameters]") {
    Document doc;
    REQUIRE(run(doc, std::make_unique<SetParameterCommand>("old", "1 mm")).empty());
    const auto a = ParamId::generate();
    REQUIRE(run(doc, std::make_unique<ReplaceParametersCommand>(std::vector<Parameter>{
                         {a, "wall", "3 mm", "note"}, {ParamId::generate(), "x", "y", ""}}))
                .empty());
    CHECK(doc.parameters().find("old") == nullptr);
    CHECK(doc.parameters().find("wall")->id == a);
    CHECK_FALSE(doc.parameters().evaluate().find("x")->ok());
    doc.undo();
    CHECK(doc.parameters().find("old"));

    auto refused = [&](std::vector<Parameter> list) {
        auto issues = run(doc, std::make_unique<ReplaceParametersCommand>(std::move(list)));
        REQUIRE_FALSE(issues.empty());
        return issues.front().code;
    };
    CHECK(refused({{ParamId::generate(), "mm", "3", ""}}) == "parameter.name");
    CHECK(refused({{ParamId::generate(), "a", "3", ""}, {ParamId::generate(), "A", "3", ""}}) ==
          "parameter.name_taken");
    CHECK(refused({{a, "a", "3", ""}, {a, "b", "3", ""}}) == "parameter.id");
    CHECK(refused({{ParamId{}, "a", "3", ""}}) == "parameter.id");
    CHECK(doc.parameters().find("old"));  // nothing changed
}
