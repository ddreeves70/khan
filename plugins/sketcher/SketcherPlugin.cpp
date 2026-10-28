#include "kh/plugins/Sketcher.h"

#include <QObject>
#include <QString>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "kh/grid/Grid.h"
#include "kh/model/Commands.h"
#include "kh/plugin/Events.h"
#include "kh/plugin/PluginContext.h"
#include "kh/ui/UiRegistry.h"
#include "kh/viewer/Viewer.h"

namespace kh::plugins {

namespace {

constexpr const char* kManifest = R"({
  "id": "kh.sketcher",
  "name": "Sketcher",
  "version": "0.1.0",
  "description": "Draws sketches in the viewers, and draws in them with the mouse: lines, circles, arcs and points.",
  "requires": ["kh.modeling", "kh.viewer"],
  "uses": ["kh.grid"],
  "tier": "ui"
})";

constexpr double kPi = std::numbers::pi;
constexpr double kSnapPixels = 8;  // how close the mouse must be to a point to snap to it

QString qs(const std::string& s) {
    return QString::fromStdString(s);
}

double distance(sketch::Point2 a, sketch::Point2 b) {
    return std::hypot(b.x - a.x, b.y - a.y);
}

// Sets an overlay, or removes it when there's nothing to draw.
void show(plugin::PluginOverlays& overlays, const char* name, plugin::Overlay overlay) {
    if (overlay.points.empty()) {
        overlays.remove(name);
    } else {
        overlays.set(name, std::move(overlay));
    }
}

// Where the student clicked or is pointing: a point on the plane, and the
// sketch point it snapped to, if any.
struct Pick {
    sketch::Point2 at{};
    std::optional<sketch::PointRef> ref{};
};

enum class Tool { Line, Circle, Arc, Point };

const char* toolHint(Tool tool, std::size_t clicks) {
    switch (tool) {
        case Tool::Line:
            return clicks == 0 ? "Line: click where it starts." : "Line: click where it ends. Esc stops.";
        case Tool::Circle:
            return clicks == 0 ? "Circle: click its center." : "Circle: click to set its size. Esc stops.";
        case Tool::Arc:
            return clicks == 0   ? "Arc: click the center of its circle."
                   : clicks == 1 ? "Arc: click where it starts. Esc stops."
                                 : "Arc: click where it ends (it goes counterclockwise). Esc stops.";
        case Tool::Point:
            return "Point: click where it goes.";
    }
    return "";
}

// The sketcher: the drawing of sketches, and the mouse tool that draws in one.
class Sketcher final : public viewer::IViewTool {
public:
    explicit Sketcher(plugin::PluginContext& ctx) : ctx_(ctx) {}

    // --- Editing ---------------------------------------------------------

    bool editing() const { return !editing_.isNil(); }

    // Starts editing a sketch step: the viewers face its plane and draw with the tool.
    void begin(const FeatureId& id) {
        const auto* step = sketchStep(id);
        auto* viewer = ctx_.service<viewer::IViewer>();
        if (!step || !viewer) return;
        editing_ = id;
        doc_ = ctx_.documents().active();
        pending_.clear();
        hover_.reset();
        viewer->face(toolPlane(step->plane()));
        viewer->startTool(*this, toolPlane(step->plane()));
        draw();
        hint();
    }

    void finish() {
        if (auto* viewer = ctx_.service<viewer::IViewer>()) viewer->stopTool(*this);
        const bool was = editing();
        editing_ = {};
        pending_.clear();
        hover_.reset();
        draw();
        if (was) message("Finished the sketch.");
    }

    void setTool(Tool tool) {
        if (!editing()) {
            problem(
                "sketch.not_editing",
                "Edit a sketch first: pick it in the feature tree, then Sketch > Edit sketch. Or make one "
                "with Sketch > New sketch.");
            return;
        }
        tool_ = tool;
        pending_.clear();
        draw();
        hint();
    }

    Tool tool() const { return tool_; }

    // Draws every sketch of the active design (and the edited one in front).
    void draw() {
        auto& overlays = ctx_.overlays();
        const DocId docId = ctx_.documents().active();
        const auto* doc = ctx_.documents().find(docId);
        plugin::Overlay others{.shape = plugin::OverlayShape::Lines, .color = {120, 136, 160}, .size = 1.5};
        plugin::Overlay lines{.shape = plugin::OverlayShape::Lines, .color = {9, 105, 218}, .size = 2.5};
        plugin::Overlay helpers{.shape = plugin::OverlayShape::Lines, .color = {140, 149, 159}, .size = 1};
        plugin::Overlay points{.shape = plugin::OverlayShape::Points, .color = {31, 35, 40}, .size = 5};
        if (doc) {
            for (std::size_t i = 0; i < doc->featureCount(); ++i) {
                const auto& f = doc->featureAt(i);
                if (f.type() != model::FeatureType::Sketch || f.suppressed()) continue;
                const auto& step = static_cast<const model::SketchFeature&>(f);
                const auto entities = shown(step);
                if (f.id() != editing_) {
                    auto add = sketchLines(step.plane(), entities);
                    others.points.insert(others.points.end(), add.begin(), add.end());
                    continue;
                }
                for (const auto& e : entities) {
                    auto add = sketchLines(step.plane(), {e});
                    auto& into = e.construction ? helpers : lines;
                    into.points.insert(into.points.end(), add.begin(), add.end());
                }
                for (const auto& [ref, at] : sketchPoints(entities)) {
                    points.points.push_back(model::toDesign(step.plane(), at));
                }
            }
        }
        show(overlays, "sketches", std::move(others));
        show(overlays, "edit.helpers", std::move(helpers));
        show(overlays, "edit.lines", std::move(lines));
        show(overlays, "edit.points", std::move(points));
        drawPreview();
    }

    // --- The mouse (IViewTool) -------------------------------------------

    void moved(const viewer::ToolPoint& point) override {
        hover_ = pick(point);
        drawPreview();
    }

    void clicked(const viewer::ToolPoint& point) override {
        if (!editing()) return;
        const Pick here = pick(point);
        hover_ = here;
        switch (tool_) {
            case Tool::Line:
                if (pending_.empty()) {
                    pending_.push_back(here);
                } else if (distance(pending_[0].at, here.at) > 1e-6) {
                    const auto line = addLine(pending_[0], here);
                    pending_.clear();
                    // Keep going from the end, unless it closed onto a point already there.
                    if (line && !here.ref)
                        pending_.push_back({here.at, sketch::PointRef{*line, sketch::PointRole::End}});
                }
                break;
            case Tool::Circle:
                if (pending_.empty()) {
                    pending_.push_back(here);
                } else if (distance(pending_[0].at, here.at) > 1e-6) {
                    addCircle(pending_[0], distance(pending_[0].at, here.at));
                    pending_.clear();
                }
                break;
            case Tool::Arc:
                if (pending_.size() < 2) {
                    if (pending_.size() == 1 && distance(pending_[0].at, here.at) <= 1e-6) break;
                    pending_.push_back(here);
                } else {
                    addArc(pending_[0], pending_[1], here);
                    pending_.clear();
                }
                break;
            case Tool::Point:
                if (!here.ref) addPoint(here);  // a point on a point already there adds nothing
                break;
        }
        draw();
        hint();
    }

    void cancelled() override {
        if (!pending_.empty()) {
            pending_.clear();
            drawPreview();
            hint();
            return;
        }
        finish();
    }

    // Stops editing if the sketch went away (deleted, undone, another design).
    void checkStillThere() {
        if (editing() && (ctx_.documents().active() != doc_ || !sketchStep(editing_))) finish();
        draw();
    }

private:
    const model::SketchFeature* sketchStep(const FeatureId& id) const {
        const auto* doc = ctx_.documents().find(ctx_.documents().active());
        const auto* f = doc ? doc->find(id) : nullptr;
        return f && f->type() == model::FeatureType::Sketch ? static_cast<const model::SketchFeature*>(f)
                                                            : nullptr;
    }

    // A sketch as the student sees it: solved, if it solved; as drawn otherwise.
    std::vector<sketch::Entity> shown(const model::SketchFeature& step) const {
        const auto* report = ctx_.documents().lastRebuild(ctx_.documents().active());
        const auto* r = report ? report->result(step.id()) : nullptr;
        if (r && r->sketch && r->sketch->ok()) return r->sketch->entities;
        return step.sketch().entities;
    }

    static std::vector<std::pair<sketch::PointRef, sketch::Point2>> sketchPoints(
        const std::vector<sketch::Entity>& entities) {
        std::vector<std::pair<sketch::PointRef, sketch::Point2>> out;
        const sketch::Sketch s{entities, {}};
        for (const auto& e : entities) {
            for (auto role : {sketch::PointRole::Self, sketch::PointRole::Start, sketch::PointRole::End,
                              sketch::PointRole::Center}) {
                const sketch::PointRef ref{e.id, role};
                if (const auto at = sketch::pointOf(s, ref)) out.emplace_back(ref, *at);
            }
        }
        return out;
    }

    static viewer::ToolPlane toolPlane(model::SketchPlane plane) {
        const auto a = model::planeAxes(plane);
        return {a.origin, a.x, a.y};
    }

    // Where a mouse point lands: on a sketch point if it's close, else on the
    // grid if grid snapping is on, else where it is.
    Pick pick(const viewer::ToolPoint& point) const {
        const auto* step = sketchStep(editing_);
        if (!step) return {};
        const auto plane = step->plane();
        const auto at = model::toSketch(plane, point.at);
        std::optional<std::pair<sketch::PointRef, sketch::Point2>> best;
        double bestDistance = kSnapPixels * point.pixel;
        for (const auto& [ref, p] : sketchPoints(shown(*step))) {
            const double d = distance(at, p);
            if (d <= bestDistance) {
                best.emplace(ref, p);
                bestDistance = d;
            }
        }
        if (best) return {best->second, best->first};
        if (const auto* grid = ctx_.service<grid::IGrid>()) {
            return {model::toSketch(plane, grid->snap(point.at, point.pane)), std::nullopt};
        }
        return {at, std::nullopt};
    }

    // The shape being drawn, following the mouse, and the point it would snap to.
    void drawPreview() {
        auto& overlays = ctx_.overlays();
        plugin::Overlay preview{.shape = plugin::OverlayShape::Lines, .color = {230, 120, 20}, .size = 1.5};
        plugin::Overlay snap{.shape = plugin::OverlayShape::Points, .color = {230, 120, 20}, .size = 11};
        const auto* step = sketchStep(editing_);
        if (step && hover_) {
            const auto plane = step->plane();
            const Pick& h = *hover_;
            auto show = [&](sketch::Shape shape) {
                auto add = sketchLines(plane, {sketch::Entity{{}, {}, shape, false}});
                preview.points.insert(preview.points.end(), add.begin(), add.end());
            };
            if (!pending_.empty()) {
                const auto& first = pending_[0].at;
                switch (tool_) {
                    case Tool::Line:
                        show(sketch::LineShape{first, h.at});
                        break;
                    case Tool::Circle:
                        if (distance(first, h.at) > 1e-9)
                            show(sketch::CircleShape{first, distance(first, h.at)});
                        break;
                    case Tool::Arc:
                        if (pending_.size() == 1) {
                            show(sketch::LineShape{first, h.at});
                        } else if (const auto arc = arcFrom(first, pending_[1].at, h.at)) {
                            show(*arc);
                        }
                        break;
                    case Tool::Point:
                        break;
                }
            }
            if (h.ref) snap.points.push_back(model::toDesign(plane, h.at));
        }
        show(overlays, "edit.preview", std::move(preview));
        show(overlays, "edit.snap", std::move(snap));
    }

    static std::optional<sketch::ArcShape> arcFrom(sketch::Point2 center, sketch::Point2 start,
                                                   sketch::Point2 end) {
        const double r = distance(center, start);
        if (r <= 1e-9 || distance(center, end) <= 1e-9) return std::nullopt;
        const double a0 = std::atan2(start.y - center.y, start.x - center.x);
        double a1 = std::atan2(end.y - center.y, end.x - center.x);
        while (a1 <= a0) a1 += 2 * kPi;  // counterclockwise from the start
        return sketch::ArcShape{center, r, a0, a1};
    }

    // --- Adding shapes: one undo step each, with Coincident for snapped ends ---

    using Join = std::pair<sketch::PointRole, std::optional<sketch::PointRef>>;

    std::optional<EntityId> add(const std::string& base, sketch::Shape shape, std::vector<Join> joins) {
        const EntityId id = EntityId::generate();
        auto change = [id, base, shape, joins](sketch::Sketch& s, std::string& label) -> Diagnostics {
            const std::string name = model::uniqueSketchName(s, base);
            s.entities.push_back({id, name, shape, false});
            for (const auto& [role, ref] : joins) {
                if (!ref) continue;
                s.constraints.push_back({ConstraintId::generate(),
                                         model::uniqueSketchName(s, "Coincident"),
                                         sketch::ConstraintKind::Coincident,
                                         {sketch::PointRef{id, role}, *ref},
                                         {},
                                         0,
                                         {}});
            }
            label = "Add " + name;
            return {};
        };
        auto problems = ctx_.documents().execute(
            doc_, std::make_unique<model::EditSketchCommand>(editing_, std::move(change)));
        if (!problems.empty()) {
            if (auto* ui = ctx_.service<ui::UiRegistry>()) ui->showProblems(problems);
            return std::nullopt;
        }
        return id;
    }

    std::optional<EntityId> addLine(const Pick& from, const Pick& to) {
        return add("Line", sketch::LineShape{from.at, to.at},
                   {{sketch::PointRole::Start, from.ref}, {sketch::PointRole::End, to.ref}});
    }

    void addCircle(const Pick& center, double radius) {
        add("Circle", sketch::CircleShape{center.at, radius}, {{sketch::PointRole::Center, center.ref}});
    }

    void addArc(const Pick& center, const Pick& start, const Pick& end) {
        if (const auto arc = arcFrom(center.at, start.at, end.at)) {
            add("Arc", *arc,
                {{sketch::PointRole::Center, center.ref},
                 {sketch::PointRole::Start, start.ref},
                 {sketch::PointRole::End, end.ref}});
        }
    }

    void addPoint(const Pick& at) { add("Point", sketch::PointShape{at.at}, {}); }

    // --- Messages ----------------------------------------------------------

    void hint() {
        if (!editing()) return;
        const auto* step = sketchStep(editing_);
        message("Drawing in " + (step ? step->name() : std::string("the sketch")) + ". " +
                toolHint(tool_, pending_.size()));
    }

    void message(const std::string& text) {
        if (auto* ui = ctx_.service<ui::UiRegistry>()) ui->showMessage(qs(text));
    }

    void problem(std::string code, std::string text) {
        if (auto* ui = ctx_.service<ui::UiRegistry>())
            ui->showProblems({{std::move(code), Severity::Error, std::move(text), {}}});
    }

    plugin::PluginContext& ctx_;
    FeatureId editing_{};  // the sketch being edited, or nil
    DocId doc_{};
    Tool tool_ = Tool::Line;
    std::vector<Pick> pending_;  // the clicks of the shape being drawn
    std::optional<Pick> hover_;  // where the mouse is
};

void registerSketcher(plugin::PluginContext& ctx) {
    if (!ctx.service<viewer::IViewer>()) {
        ctx.fail("The sketcher needs the 3D viewer plugin (kh.viewer). Reinstall Khan to get it back.");
        return;
    }
    auto sketcher = std::make_shared<Sketcher>(ctx);
    using plugin::ActionSpec;

    auto newSketch = [&ctx, sketcher](const char* plane) {
        auto& docs = ctx.documents();
        const DocId doc = docs.active();
        if (!docs.find(doc)) return;
        sketcher->finish();
        const auto problems = docs.run(doc, "feature.add_sketch", {{"plane", std::string(plane)}});
        if (!problems.empty()) {
            if (auto* ui = ctx.service<ui::UiRegistry>()) ui->showProblems(problems);
            return;
        }
        const auto* design = docs.find(doc);
        const FeatureId id = design->featureAt(design->featureCount() - 1).id();
        ctx.selection().set(doc, {id});
        sketcher->begin(id);
    };
    ctx.addAction(ActionSpec{.id = "kh.sketcher.new_xy",
                             .text = "New sketch on XY (the ground)",
                             .menu = "Sketch",
                             .shortcut = "Ctrl+Shift+K",
                             .tip = "Start a sketch lying flat, seen from the top, and draw in it.",
                             .run = [newSketch] { newSketch("xy"); }});
    ctx.addAction(ActionSpec{.id = "kh.sketcher.new_xz",
                             .text = "New sketch on XZ (standing, from the front)",
                             .menu = "Sketch",
                             .tip = "Start a sketch standing up, seen from the front, and draw in it.",
                             .run = [newSketch] { newSketch("xz"); }});
    ctx.addAction(ActionSpec{.id = "kh.sketcher.new_yz",
                             .text = "New sketch on YZ (standing, from the right)",
                             .menu = "Sketch",
                             .tip = "Start a sketch standing up, seen from the right, and draw in it.",
                             .run = [newSketch] { newSketch("yz"); }});
    ctx.addAction(ActionSpec{
        .id = "kh.sketcher.edit",
        .text = "Edit sketch",
        .menu = "Sketch",
        .shortcut = "Ctrl+K",
        .tip = "Draw in the picked sketch.",
        .run =
            [&ctx, sketcher] {
                const auto& sel = ctx.selection();
                const auto* doc = ctx.documents().find(ctx.documents().active());
                const auto* f = doc && sel.doc() == ctx.documents().active() && sel.features().size() == 1
                                    ? doc->find(sel.features()[0])
                                    : nullptr;
                if (!f || f->type() != model::FeatureType::Sketch) {
                    if (auto* ui = ctx.service<ui::UiRegistry>())
                        ui->showProblems({{"sketch.pick",
                                           Severity::Error,
                                           "Pick one sketch in the feature tree first, like "
                                           "Sketch1.",
                                           {}}});
                    return;
                }
                sketcher->begin(f->id());
            },
        .checked = [sketcher] { return sketcher->editing(); }});
    ctx.addAction(ActionSpec{.id = "kh.sketcher.finish",
                             .text = "Finish sketch",
                             .menu = "Sketch",
                             .tip = "Stop drawing. Clicks in the viewer pick and turn the view again.",
                             .run = [sketcher] { sketcher->finish(); }});
    struct ToolItem {
        const char* id;
        const char* text;
        Tool tool;
        const char* tip;
    };
    for (const auto& t :
         {ToolItem{"kh.sketcher.line", "Line", Tool::Line,
                   "Draw lines: click where each one starts and ends. They join up as you go."},
          ToolItem{"kh.sketcher.circle", "Circle", Tool::Circle,
                   "Draw a circle: click its center, then its edge."},
          ToolItem{"kh.sketcher.arc", "Arc", Tool::Arc,
                   "Draw an arc: click the center, where it starts, then where it ends."},
          ToolItem{"kh.sketcher.point", "Point", Tool::Point, "Draw a point: click where it goes."}}) {
        const Tool tool = t.tool;
        ctx.addAction(ActionSpec{
            .id = t.id,
            .text = t.text,
            .menu = "Sketch/Draw",
            .tip = t.tip,
            .run = [sketcher, tool] { sketcher->setTool(tool); },
            .checked = [sketcher, tool] { return sketcher->editing() && sketcher->tool() == tool; }});
    }

    // Redraw after every rebuild, and stop editing a sketch that went away.
    ctx.listen<plugin::RebuildFinished>(
        [sketcher](const plugin::RebuildFinished&) { sketcher->checkStillThere(); });
    ctx.listen<plugin::ActiveDocumentChanged>(
        [sketcher](const plugin::ActiveDocumentChanged&) { sketcher->checkStillThere(); });
    sketcher->draw();
}

}  // namespace

std::vector<kernel::Vec3> sketchLines(model::SketchPlane plane, const std::vector<sketch::Entity>& entities) {
    std::vector<kernel::Vec3> out;
    auto segment = [&](sketch::Point2 a, sketch::Point2 b) {
        out.push_back(model::toDesign(plane, a));
        out.push_back(model::toDesign(plane, b));
    };
    // A circle or arc as short straight pieces, about 5 degrees each.
    auto curve = [&](sketch::Point2 c, double r, double a0, double a1) {
        const int pieces = std::max(4, static_cast<int>(std::ceil(std::abs(a1 - a0) / (kPi / 36))));
        sketch::Point2 prev{c.x + r * std::cos(a0), c.y + r * std::sin(a0)};
        for (int i = 1; i <= pieces; ++i) {
            const double a = a0 + (a1 - a0) * i / pieces;
            const sketch::Point2 next{c.x + r * std::cos(a), c.y + r * std::sin(a)};
            segment(prev, next);
            prev = next;
        }
    };
    for (const auto& e : entities) {
        if (const auto* l = std::get_if<sketch::LineShape>(&e.shape)) {
            segment(l->start, l->end);
        } else if (const auto* c = std::get_if<sketch::CircleShape>(&e.shape)) {
            curve(c->center, c->radius, 0, 2 * kPi);
        } else if (const auto* a = std::get_if<sketch::ArcShape>(&e.shape)) {
            curve(a->center, a->radius, a->startAngle, a->endAngle);
        }
    }
    return out;
}

plugin::BuiltinPlugin sketcherPlugin() {
    return {kManifest, &registerSketcher};
}

}  // namespace kh::plugins
