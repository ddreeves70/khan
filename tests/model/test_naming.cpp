// Topological naming with real geometry: every face, edge and vertex of a
// step's result gets a name from the design's history, the names stay the same
// when sizes change, and references are found again, or reported, never guessed.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <set>

#include "sb/kernel_occt/OcctKernel.h"
#include "sb/model/CommandRegistry.h"
#include "sb/model/Document.h"
#include "sb/sketch_planegcs/PlaneGcsSolver.h"

using namespace sb;
using namespace sb::model;
using kernel::TopoKind;

namespace {

struct Design {
    Document doc;
    std::unique_ptr<kernel::IKernel> kernel = kernel::makeOcctKernel();
    std::unique_ptr<sketch::ISketchSolver> solver = sketch::makePlaneGcsSolver();

    void run(std::string_view name, Args args) {
        auto made = CommandRegistry::builtin().create(name, args, doc);
        INFO(name << ": " << (made.issues.empty() ? std::string() : made.issues.front().message));
        REQUIRE(made.ok());
        const auto issues = doc.execute(std::move(made.command));
        INFO((issues.empty() ? std::string() : issues.front().message));
        REQUIRE(issues.empty());
    }
    FeatureId last() const { return doc.featureAt(doc.featureCount() - 1).id(); }
    RebuildReport rebuild() {
        auto r = doc.regenerate(*kernel, solver.get());
        REQUIRE(r.ok());
        return r;
    }
    // A 40 x 20 mm rectangle (Line1 to Line4) with its width as Length1,
    // extruded 10 mm up.
    FeatureId plate() {
        run("feature.add_sketch", {});
        const auto sketch = last();
        run("sketch.add_rectangle",
            {{"sketch", sketch}, {"from", sketch::Point2{0, 0}}, {"to", sketch::Point2{40, 20}}});
        run("sketch.constrain", {{"sketch", sketch},
                                 {"kind", std::string("length")},
                                 {"on", std::string("Line1")},
                                 {"value", 40.0}});
        run("feature.add_extrude", {{"sketch", sketch}, {"distance", 10.0}});
        return last();
    }
};

const ShapeNames& namesOf(const RebuildReport& r, const FeatureId& id) {
    const auto* result = r.result(id);
    REQUIRE(result);
    REQUIRE(result->names);
    return *result->names;
}

std::set<std::string> all(const ShapeNames& n) {
    std::set<std::string> out(n.faces.begin(), n.faces.end());
    out.insert(n.edges.begin(), n.edges.end());
    out.insert(n.vertices.begin(), n.vertices.end());
    return out;
}

// An edge's name: the names of the faces it borders, in order.
std::string edgeName(std::string a, std::string b) {
    if (b < a) std::swap(a, b);
    return "E[" + a + "|" + b + "]";
}

bool unique(const std::vector<std::string>& names) {
    return std::set<std::string>(names.begin(), names.end()).size() == names.size();
}

// The face whose outward normal is (0, 0, ±1) and whose middle is highest or lowest.
int faceFacing(const ShapeNames& n, double z) {
    for (std::size_t i = 0; i < n.elements.faces.size(); ++i) {
        const auto& f = n.elements.faces[i];
        if (f.geom == kernel::GeomKind::Plane && f.direction.z * z > 0.999) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace

TEST_CASE("name stems drop the #k numbers", "[model][naming]") {
    CHECK(nameStem("abc/top") == "abc/top");
    CHECK(nameStem("abc/top#2") == "abc/top");
    CHECK(nameStem("abc/top#2#10") == "abc/top");
    CHECK(nameStem("abc/top#x") == "abc/top#x");
    CHECK(nameStem("abc/top#") == "abc/top#");
    CHECK(shortId("0123456789abcdef") == "01234567");
    CHECK(shortId("abc") == "abc");
}

TEST_CASE("every face, edge and vertex of a box gets its own name", "[model][naming][occt]") {
    Design d;
    d.run("feature.add_box", {{"size", kernel::Vec3{10, 20, 30}}});
    const auto box = d.last();
    const auto report = d.rebuild();
    const auto& n = namesOf(report, box);
    REQUIRE(n.faces.size() == 6);
    REQUIRE(n.edges.size() == 12);
    REQUIRE(n.vertices.size() == 8);
    CHECK(unique(n.faces));
    CHECK(unique(n.edges));
    CHECK(unique(n.vertices));
    const std::string id = shortId(box.toString());
    const int top = faceFacing(n, 1);
    REQUIRE(top >= 0);
    CHECK(n.faces[static_cast<std::size_t>(top)] == id + "/top");
    CHECK(n.find(TopoKind::Face, id + "/right") >= 0);
    // An edge is named for the two faces it borders.
    CHECK(n.find(TopoKind::Edge, edgeName(id + "/top", id + "/right")) >= 0);
    // Rebuilding gives the same names in the same order.
    CHECK(namesOf(d.rebuild(), box).faces == n.faces);
    CHECK(namesOf(d.rebuild(), box).edges == n.edges);
}

TEST_CASE("an extrude names its sides for the sketch lines they came from", "[model][naming][occt]") {
    Design d;
    const auto extrude = d.plate();
    const auto report = d.rebuild();
    const auto& n = namesOf(report, extrude);
    REQUIRE(n.faces.size() == 6);
    CHECK(unique(n.faces));
    CHECK(unique(n.edges));
    CHECK(unique(n.vertices));
    const auto& sketch = static_cast<const SketchFeature&>(d.doc.featureAt(0)).sketch();
    const std::string me = shortId(extrude.toString());
    for (const auto& e : sketch.entities) {
        INFO(e.name);
        CHECK(n.find(TopoKind::Face, me + "/side:" + shortId(e.id.toString())) >= 0);
    }
    const int top = faceFacing(n, 1), bottom = faceFacing(n, -1);
    REQUIRE(top >= 0);
    REQUIRE(bottom >= 0);
    CHECK(n.faces[static_cast<std::size_t>(top)].starts_with(me + "/end:"));
    CHECK(n.faces[static_cast<std::size_t>(bottom)].starts_with(me + "/start:"));
}

TEST_CASE("names stay the same when sizes change", "[model][naming][occt]") {
    Design d;
    const auto extrude = d.plate();
    const auto before = all(namesOf(d.rebuild(), extrude));
    REQUIRE(before.size() == 6 + 12 + 8);

    // A different distance, a different width, and both sides at once.
    for (const double distance : {5.0, 37.5}) {
        d.run("feature.set_value",
              {{"feature", extrude}, {"setting", std::string("distance")}, {"value", distance}});
        CHECK(all(namesOf(d.rebuild(), extrude)) == before);
    }
    d.run("sketch.set_value",
          {{"sketch", d.doc.featureAt(0).id()}, {"constraint", std::string("Length1")}, {"value", 65.0}});
    CHECK(all(namesOf(d.rebuild(), extrude)) == before);
}

TEST_CASE("which way an extrude goes doesn't change its names, except its own id", "[model][naming][occt]") {
    for (const char* side : {"flip", "both"}) {
        Design d;
        d.run("feature.add_sketch", {});
        const auto sketch = d.last();
        d.run("sketch.add_rectangle",
              {{"sketch", sketch}, {"from", sketch::Point2{0, 0}}, {"to", sketch::Point2{40, 20}}});
        d.run("feature.add_extrude", {{"sketch", sketch}, {"distance", 10.0}});
        const auto one = d.last();
        d.run("feature.add_extrude", {{"sketch", sketch}, {"distance", 10.0}, {"side", std::string(side)}});
        const auto other = d.last();
        const auto report = d.rebuild();
        auto strip = [](const ShapeNames& n, const FeatureId& id) {
            std::set<std::string> out;
            for (auto name : n.faces) {
                const std::string me = shortId(id.toString());
                for (auto at = name.find(me); at != std::string::npos; at = name.find(me))
                    name.replace(at, me.size(), "X");
                out.insert(name);
            }
            return out;
        };
        INFO(side);
        CHECK(strip(namesOf(report, one), one) == strip(namesOf(report, other), other));
    }
}

TEST_CASE("a cut and a move keep the names faces had on their bodies", "[model][naming][occt]") {
    Design d;
    d.run("feature.add_box", {{"size", kernel::Vec3{40, 30, 10}}});
    const auto block = d.last();
    d.run("feature.add_cylinder", {{"base", kernel::Vec3{20, 15, -1}}, {"radius", 4.0}, {"height", 12.0}});
    const auto pin = d.last();
    d.run("feature.add_boolean", {{"operation", std::string("cut")}, {"a", block}, {"b", pin}});
    const auto cut = d.last();
    d.run("feature.add_move", {{"body", cut}, {"by", kernel::Vec3{100, 0, 0}}});
    const auto moved = d.last();
    const auto report = d.rebuild();

    const auto& c = namesOf(report, cut);
    REQUIRE(c.faces.size() == 7);
    CHECK(unique(c.faces));
    CHECK(c.find(TopoKind::Face, shortId(pin.toString()) + "/side") >= 0);  // the hole's wall
    CHECK(c.find(TopoKind::Face, shortId(block.toString()) + "/top") >= 0);
    // The hole's top edge, between the box's top and the cylinder's side.
    const std::string rim = edgeName(shortId(block.toString()) + "/top", shortId(pin.toString()) + "/side");
    CHECK(c.find(TopoKind::Edge, rim) >= 0);
    // Moving keeps every name.
    CHECK(all(namesOf(report, moved)) == all(c));
}

TEST_CASE("a reference is found exactly, matched, or reported, never guessed", "[model][naming][occt]") {
    Design d;
    d.run("feature.add_box", {{"size", kernel::Vec3{40, 30, 10}}});
    const auto block = d.last();
    const auto first = d.rebuild();
    const auto& boxNames = namesOf(first, block);
    const int topIndex = faceFacing(boxNames, 1);
    const GeometryRef top = referTo(boxNames, TopoKind::Face, topIndex);
    REQUIRE(top.name == shortId(block.toString()) + "/top");
    CHECK(top.signature.size == Catch::Approx(1200));

    SECTION("exact: the same name after a change of size") {
        d.run("feature.set_value", {{"feature", block}, {"setting", std::string("size_x")}, {"value", 55.0}});
        const auto r = resolve(top, namesOf(d.rebuild(), block));
        CHECK(r.status == ResolveStatus::Exact);
        CHECK(r.index == topIndex);
    }

    SECTION("ambiguous: a slot cuts the top in two equal halves") {
        d.run("feature.add_box", {{"corner", kernel::Vec3{15, -1, 2}}, {"size", kernel::Vec3{10, 32, 10}}});
        const auto slot = d.last();
        d.run("feature.add_boolean", {{"operation", std::string("cut")}, {"a", block}, {"b", slot}});
        const auto cut = d.last();
        const auto after = d.rebuild();
        const auto& n = namesOf(after, cut);
        // Both halves came from the top, so both are "top", numbered by where they are.
        CHECK(n.find(TopoKind::Face, top.name + "#1") >= 0);
        CHECK(n.find(TopoKind::Face, top.name + "#2") >= 0);
        const auto r = resolve(top, n);
        CHECK(r.status == ResolveStatus::Ambiguous);
        CHECK(r.candidates.size() == 2);
        CHECK_FALSE(r.found());

        // A half picked after the cut is matched again when the slot moves off center
        // so it isn't the same size any more.
        const GeometryRef left = referTo(n, TopoKind::Face, n.find(TopoKind::Face, top.name + "#1"));
        d.run("feature.set_value", {{"feature", slot}, {"setting", std::string("corner_x")}, {"value", 5.0}});
        const auto moved = resolve(left, namesOf(d.rebuild(), cut));
        CHECK(moved.status == ResolveStatus::Exact);  // still "#1": the left half

        // With the slot off the top (only in the side), the top is whole again: the
        // left half's reference is matched to it.
        d.run("feature.set_value",
              {{"feature", slot}, {"setting", std::string("corner_z")}, {"value", -5.0}});
        d.run("feature.set_value", {{"feature", slot}, {"setting", std::string("size_z")}, {"value", 8.0}});
        const auto rebuilt = d.rebuild();
        const auto& whole = namesOf(rebuilt, cut);
        const auto again = resolve(left, whole);
        CHECK(again.status == ResolveStatus::Matched);
        CHECK(again.index == whole.find(TopoKind::Face, top.name));
    }

    SECTION("missing: the face's step is gone") {
        GeometryRef gone = top;
        gone.name = "00000000/top";
        CHECK(resolve(gone, namesOf(d.rebuild(), block)).status == ResolveStatus::Missing);
        CHECK(resolve(GeometryRef{}, boxNames).status == ResolveStatus::Missing);
    }
}

TEST_CASE("an edge whose face was split is matched by where it is", "[model][naming][occt]") {
    Design d;
    d.run("feature.add_box", {{"size", kernel::Vec3{40, 30, 10}}});
    const auto block = d.last();
    const auto first = d.rebuild();
    const auto& boxNames = namesOf(first, block);
    const std::string id = shortId(block.toString());
    // The top's edge at y = 0, along x, 40 mm long.
    const int edgeIndex = boxNames.find(TopoKind::Edge, edgeName(id + "/top", id + "/front"));
    REQUIRE(edgeIndex >= 0);
    const GeometryRef edge = referTo(boxNames, TopoKind::Edge, edgeIndex);

    // A slot across the box, off center, splits the edge into a short and a long piece.
    d.run("feature.add_box", {{"corner", kernel::Vec3{5, -1, 2}}, {"size", kernel::Vec3{5, 32, 10}}});
    const auto slot = d.last();
    d.run("feature.add_boolean", {{"operation", std::string("cut")}, {"a", block}, {"b", slot}});
    const auto cut = d.last();
    const auto after = d.rebuild();
    const auto& n = namesOf(after, cut);
    const auto r = resolve(edge, n);
    // Two pieces between faces from the same places. The 30 mm piece is much more
    // like the 40 mm edge (its middle moved 5 mm) than the 5 mm one (17.5 mm away).
    REQUIRE(r.status == ResolveStatus::Matched);
    CHECK(n.elements.edges.at(static_cast<std::size_t>(r.index)).size == Catch::Approx(30));
}

// Bug (found before it shipped): a box's side was named "x+", and "+" also joins
// the names of merged faces, so it was described as "new face of Plate and
// others joined".
TEST_CASE("faces and edges are described in plain words", "[model][naming][occt]") {
    Design d;
    d.run("feature.add_box", {{"size", kernel::Vec3{60, 30, 4}}, {"name", std::string("Plate")}});
    const auto plate = d.last();
    d.run(
        "feature.add_cylinder",
        {{"base", kernel::Vec3{9, 15, -1}}, {"radius", 2.5}, {"height", 6.0}, {"name", std::string("Hole")}});
    const auto hole = d.last();
    d.run("feature.add_boolean",
          {{"operation", std::string("cut")}, {"a", plate}, {"b", hole}, {"name", std::string("Tag")}});
    const auto tag = d.last();
    const auto report = d.rebuild();
    const auto& n = namesOf(report, tag);
    std::set<std::string> faces;
    for (const auto& f : n.faces) faces.insert(describeElement(d.doc, TopoKind::Face, f));
    CHECK(faces == std::set<std::string>{"bottom of Plate", "top of Plate", "left side of Plate",
                                         "right side of Plate", "front side of Plate", "back side of Plate",
                                         "side of Hole"});
    const std::string id = shortId(plate.toString());
    CHECK(describeElement(d.doc, TopoKind::Edge, edgeName(id + "/top", id + "/right")) ==
          "between right side of Plate and top of Plate");
    CHECK(describeElement(d.doc, TopoKind::Edge, "E[" + shortId(hole.toString()) + "/side]") ==
          "seam of side of Hole");
    CHECK(describeElement(d.doc, TopoKind::Face, "00000000/top") == "top of a deleted step");
    CHECK(describeElement(d.doc, TopoKind::Face, id + "/top#2") == "top of Plate (piece 2)");
}
