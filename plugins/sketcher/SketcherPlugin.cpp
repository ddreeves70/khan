#include "kh/plugins/Sketcher.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "Constrain.h"
#include "SketchPanel.h"
#include "SketcherIcons.h"
#include "kh/grid/Grid.h"
#include "kh/model/Commands.h"
#include "kh/plugin/Events.h"
#include "kh/plugin/PluginContext.h"
#include "kh/sketch/Profiles.h"
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
// The toolbar mode while a sketch is being edited: only the sketch buttons show.
constexpr std::string_view kEditingMode = "kh.sketcher.editing";
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
    std::optional<sketch::PointRef> ref{};  // the sketch point it snapped onto
    std::optional<EntityId> on{};           // ... or the line, circle or arc it snapped onto
    // For a line's end: kept level, upright, or running on smoothly from an arc.
    std::optional<sketch::ConstraintKind> align{};
    std::optional<EntityId> alignWith{};  // the arc, for Tangent
};

// A part of the sketch the student can pick: one point of an entity, a whole
// entity (by its edge), or a constraint (by its label).
struct Item {
    EntityId entity{};
    std::optional<sketch::PointRole> point{};
    ConstraintId constraint{};  // set for a constraint; then entity is nil
    bool operator==(const Item&) const = default;
    bool isConstraint() const { return !constraint.isNil(); }
};

using sketcher::Rule;

enum class Tool { Select, Line, Circle, Arc, Point };

const char* toolHint(Tool tool, std::size_t clicks) {
    switch (tool) {
        case Tool::Select:
            return "Click a line or point to pick it, or drag it to move it. Esc finishes the sketch.";
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

// How far a point is from an entity's edge (mm). Points have no edge.
std::optional<double> edgeDistance(const sketch::Shape& shape, sketch::Point2 p) {
    if (const auto* l = std::get_if<sketch::LineShape>(&shape)) {
        const double dx = l->end.x - l->start.x, dy = l->end.y - l->start.y;
        const double len2 = dx * dx + dy * dy;
        double t = len2 > 0 ? ((p.x - l->start.x) * dx + (p.y - l->start.y) * dy) / len2 : 0;
        t = std::clamp(t, 0.0, 1.0);
        return distance(p, {l->start.x + t * dx, l->start.y + t * dy});
    }
    if (const auto* c = std::get_if<sketch::CircleShape>(&shape))
        return std::abs(distance(p, c->center) - c->radius);
    if (const auto* a = std::get_if<sketch::ArcShape>(&shape)) {
        // On the arc's part of the circle? Then how far from the circle; else from the nearer end.
        double angle = std::atan2(p.y - a->center.y, p.x - a->center.x);
        while (angle < a->startAngle) angle += 2 * kPi;
        while (angle >= a->startAngle + 2 * kPi) angle -= 2 * kPi;
        if (angle <= a->endAngle) return std::abs(distance(p, a->center) - a->radius);
        auto end = [&](double t) {
            return sketch::Point2{a->center.x + a->radius * std::cos(t),
                                  a->center.y + a->radius * std::sin(t)};
        };
        return std::min(distance(p, end(a->startAngle)), distance(p, end(a->endAngle)));
    }
    return std::nullopt;
}

// The sketcher: the drawing of sketches, and the mouse tool that edits one.
class Sketcher final : public viewer::IViewTool {
public:
    explicit Sketcher(plugin::PluginContext& ctx) : ctx_(ctx) {}

    // --- Editing ---------------------------------------------------------

    bool editing() const { return !editing_.isNil(); }

    // Starts editing a sketch step: the viewers face its plane, and the mouse
    // works with the given tool.
    void begin(const FeatureId& id, Tool tool) {
        const auto* step = sketchStep(id);
        auto* viewer = ctx_.service<viewer::IViewer>();
        if (!step || !viewer) return;
        editing_ = id;
        doc_ = ctx_.documents().active();
        tool_ = tool;
        reset();
        viewer->face(toolPlane(step->plane()));
        viewer->startTool(*this, toolPlane(step->plane()));
        draw();
        hint();
        ctx_.setToolbarMode(std::string(kEditingMode));  // the toolbar shows the sketch buttons
        if (auto* ui = ctx_.service<ui::UiRegistry>()) ui->showPanel("kh.sketcher.panel");
        refreshPanels();
        ctx_.publish(plugin::ActionsChanged{});  // Finish and the tools turn on, Edit turns off
    }

    void finish() {
        if (auto* viewer = ctx_.service<viewer::IViewer>()) viewer->stopTool(*this);
        const bool was = editing();
        editing_ = {};
        reset();
        draw();
        if (was) {
            message("Finished the sketch.");
            refreshPanels();
            ctx_.setToolbarMode({});  // back to the usual toolbar
            // The Sketch panel is only for editing: put it away, so it doesn't cover the view.
            if (auto* ui = ctx_.service<ui::UiRegistry>()) ui->hidePanel("kh.sketcher.panel");
            ctx_.publish(plugin::ActionsChanged{});
        }
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
        hoverItem_.reset();
        draw();
        hint();
        ctx_.publish(plugin::ActionsChanged{});  // the pressed tool button changes
    }

    Tool tool() const { return tool_; }
    const std::vector<Item>& picked() const { return picked_; }

    // Deletes the picked lines, circles, arcs and points (with the constraints
    // on them). A picked end of a line or arc loses its constraints instead.
    void deletePicked() {
        if (!editing() || picked_.empty()) return;
        const auto items = picked_;
        auto change = [items](sketch::Sketch& s, std::string& label) -> Diagnostics {
            std::vector<std::string> names;
            auto uses = [](const sketch::Constraint& c, const EntityId& id) {
                return std::ranges::find(c.entities, id) != c.entities.end() ||
                       std::ranges::any_of(c.points, [&](const auto& p) { return p.entity == id; });
            };
            for (const auto& item : items) {
                if (item.isConstraint()) {
                    if (const auto* c = s.find(item.constraint)) {
                        names.push_back(c->name);
                        std::erase_if(s.constraints,
                                      [&](const sketch::Constraint& x) { return x.id == item.constraint; });
                    }
                    continue;
                }
                const auto* e = s.find(item.entity);
                if (!e) continue;
                const bool whole = !item.point || item.point == sketch::PointRole::Self;
                if (whole) {
                    names.push_back(e->name);
                    const EntityId id = e->id;
                    std::erase_if(s.constraints, [&](const sketch::Constraint& c) { return uses(c, id); });
                    std::erase_if(s.entities, [&](const sketch::Entity& x) { return x.id == id; });
                } else {
                    names.push_back("the constraints on " + e->name + "'s " + roleName(*item.point));
                    std::erase_if(s.constraints, [&](const sketch::Constraint& c) {
                        return std::ranges::any_of(c.points, [&](const auto& p) {
                            return p.entity == item.entity && p.role == *item.point;
                        });
                    });
                }
            }
            label = "Delete " + (names.size() == 1 ? names[0] : std::to_string(names.size()) + " things");
            return {};
        };
        picked_.clear();
        hoverItem_.reset();
        run(std::move(change));
        hint();
        ctx_.publish(plugin::ActionsChanged{});
    }

    // --- Constraints and dimensions ---------------------------------------

    // Whether a constraint or dimension button fits what's picked.
    bool fits(Rule rule) const {
        const auto* step = sketchStep(editing_);
        if (!step || picked_.empty()) return false;
        return !sketcher::plan(rule, picks(*step), {current(*step), step->sketch().constraints}).empty();
    }

    // Adds the constraints a button means for what's picked, as one undo step.
    // A dimension starts at today's size, so nothing moves; its number is ready
    // to type in the Sketch panel.
    void constrain(Rule rule) {
        const auto* step = sketchStep(editing_);
        if (!step) return;
        const auto info = sketcher::ruleInfo(rule);
        auto adds = sketcher::plan(rule, picks(*step), {current(*step), step->sketch().constraints});
        if (adds.empty()) {
            problem("sketch.pick_for", std::string(info.text) + " needs " + info.needs +
                                           " picked. Pick them with " +
                                           "Select (Ctrl-click picks more), then try again.");
            return;
        }
        for (auto& c : adds) c.id = ConstraintId::generate();
        const bool dimension = sketch::isDimension(adds.front().kind);
        auto change = [adds](sketch::Sketch& s, std::string& label) -> Diagnostics {
            std::vector<std::string> added, already;
            for (auto c : adds) {
                const auto there = std::ranges::find_if(s.constraints, [&](const sketch::Constraint& x) {
                    return sketcher::sameConstraint(x, c);
                });
                if (there != s.constraints.end()) {
                    already.push_back(there->name);
                    continue;
                }
                c.name = model::uniqueSketchName(s, sketcher::constraintBase(c.kind));
                added.push_back(c.name);
                s.constraints.push_back(std::move(c));
            }
            if (added.empty()) {
                std::string names;
                for (const auto& n : already) names += (names.empty() ? "" : ", ") + n;
                return {{"sketch.already",
                         Severity::Error,
                         "That's already there: " + names + ". Pick something else, or delete it first.",
                         {}}};
            }
            label = "Add " + (added.size() == 1 ? added[0] : std::to_string(added.size()) + " constraints");
            return {};
        };
        picked_.clear();
        if (dimension) justAdded_ = adds.front().id;  // the Sketch panel puts the cursor in its number
        if (!run(std::move(change))) {
            justAdded_ = {};
            return;
        }
        refreshPanels();
        if (dimension) {
            message("Added " + std::string(info.text) +
                    ". Type its number in the Sketch panel, then press Enter.");
        } else {
            hint();
        }
        draw();
        ctx_.publish(plugin::ActionsChanged{});
    }

    // The dimension just added, for the Sketch panel to put the cursor in (once).
    ConstraintId takeJustAdded() { return std::exchange(justAdded_, {}); }

    // --- The Sketch panel ----------------------------------------------------

    std::optional<FeatureId> editingStep() const {
        return editing() ? std::optional<FeatureId>(editing_) : std::nullopt;
    }
    std::vector<ConstraintId> pickedConstraints() const {
        std::vector<ConstraintId> out;
        for (const auto& i : picked_) {
            if (i.isConstraint()) out.push_back(i.constraint);
        }
        return out;
    }
    // A click on a constraint's row in the panel picks it in the viewer too.
    void pickConstraint(const ConstraintId& id) {
        picked_ = {Item{{}, std::nullopt, id}};
        draw();
        hint();
        ctx_.publish(plugin::ActionsChanged{});
    }
    void addPanel(sketcher::SketchPanel* panel) { panels_.emplace_back(panel); }
    void refreshPanels() {
        std::erase_if(panels_, [](const auto& p) { return p.isNull(); });
        for (auto& p : panels_) p->refresh();
    }

    // Draws every sketch of the active design (and the edited one in front),
    // the edited one colored by what can still move.
    void draw() {
        auto& overlays = ctx_.overlays();
        const DocId docId = ctx_.documents().active();
        const auto* doc = ctx_.documents().find(docId);
        using plugin::Overlay;
        using plugin::OverlayShape;
        Overlay others{.shape = OverlayShape::Lines, .color = {120, 136, 160}, .size = 1.5};
        Overlay helpers{.shape = OverlayShape::Lines, .color = {140, 149, 159}, .size = 1};
        Overlay freeLines{.shape = OverlayShape::Lines, .color = kFree, .size = 2.5};
        Overlay fixedLines{.shape = OverlayShape::Lines, .color = kFixed, .size = 2.5};
        Overlay freePoints{.shape = OverlayShape::Points, .color = kFree, .size = 6};
        Overlay fixedPoints{.shape = OverlayShape::Points, .color = kFixed, .size = 6};
        Overlay hoverLines{.shape = OverlayShape::Lines, .color = kHover, .size = 3.5};
        Overlay hoverPoints{.shape = OverlayShape::Points, .color = kHover, .size = 10};
        Overlay pickedLines{.shape = OverlayShape::Lines, .color = kPicked, .size = 3.5};
        Overlay pickedPoints{.shape = OverlayShape::Points, .color = kPicked, .size = 10};
        Overlay conflictLines{.shape = OverlayShape::Lines, .color = kConflict, .size = 3};
        Overlay openEnds{.shape = OverlayShape::Points, .color = kConflict, .size = 9};
        auto labelsIn = [](plugin::OverlayColor color) {
            return Overlay{.shape = OverlayShape::Labels, .color = color, .size = 13};
        };
        Overlay labels = labelsIn(kLabel), redundantLabels = labelsIn(kRedundant),
                conflictLabels = labelsIn(kConflict), hoverLabels = labelsIn(kHover),
                pickedLabels = labelsIn(kPicked);
        labels_.clear();
        if (doc) {
            for (std::size_t i = 0; i < doc->featureCount(); ++i) {
                const auto& f = doc->featureAt(i);
                if (f.type() != model::FeatureType::Sketch || f.suppressed()) continue;
                const auto& step = static_cast<const model::SketchFeature&>(f);
                if (f.id() != editing_) {
                    auto add = sketchLines(step.plane(), shown(step));
                    others.points.insert(others.points.end(), add.begin(), add.end());
                    continue;
                }
                const auto entities = current(step);
                const auto* solved = solveResult(step);
                // Free (blue) unless the last solve says it's fully constrained (black).
                auto entityFree = [&](const EntityId& id) {
                    return !solved ||
                           std::ranges::find(solved->freeEntities, id) != solved->freeEntities.end();
                };
                auto pointFree = [&](const sketch::PointRef& r) {
                    return !solved || std::ranges::any_of(solved->freePoints, [&](const auto& p) {
                        return p.entity == r.entity && p.role == r.role;
                    });
                };
                auto lineInto = [&](Overlay& o, const sketch::Entity& e) {
                    auto add = sketchLines(step.plane(), {e});
                    o.points.insert(o.points.end(), add.begin(), add.end());
                };
                for (const auto& e : entities) {
                    lineInto(e.construction ? helpers : entityFree(e.id) ? freeLines : fixedLines, e);
                }
                for (const auto& [ref, at] : sketchPoints(entities)) {
                    (pointFree(ref) ? freePoints : fixedPoints)
                        .points.push_back(model::toDesign(step.plane(), at));
                }
                // Ends that aren't joined, so the shape isn't closed yet: red dots.
                for (const auto& ref : sketch::findProfiles({entities, {}}).openEnds) {
                    if (const auto at = sketch::pointOf({entities, {}}, ref))
                        openEnds.points.push_back(model::toDesign(step.plane(), *at));
                }
                // Picked, and under the mouse (in Select).
                auto showItem = [&](const Item& item, Overlay& lines, Overlay& points) {
                    if (item.isConstraint()) return;  // its label is drawn orange instead
                    const sketch::Sketch s{entities, {}};
                    const auto* e = s.find(item.entity);
                    if (!e) return;
                    if (item.point) {
                        if (const auto at = sketch::pointOf(s, {item.entity, *item.point}))
                            points.points.push_back(model::toDesign(step.plane(), *at));
                    } else if (std::holds_alternative<sketch::PointShape>(e->shape)) {
                        points.points.push_back(
                            model::toDesign(step.plane(), std::get<sketch::PointShape>(e->shape).at));
                    } else {
                        lineInto(lines, *e);
                    }
                };
                if (hoverItem_ && !drag_) showItem(*hoverItem_, hoverLines, hoverPoints);
                for (const auto& item : picked_) showItem(item, pickedLines, pickedPoints);

                // Constraint labels: "H", "40 mm", ... beside what they're on. Ones that
                // can't all be true are red (with what they're on); repeats are amber.
                const auto* last = lastResult(step);
                auto listed = [](const std::vector<ConstraintId>& ids, const ConstraintId& id) {
                    return std::ranges::find(ids, id) != ids.end();
                };
                const sketch::Sketch whole{entities, step.sketch().constraints};
                // (Not a structured binding: older Apple clang can't use those in lambdas.)
                const auto spacing = labelSpacing(entities);
                const double gap = spacing.first;
                const sketch::Point2 middle = spacing.second;
                // Labels already placed, as boxes: their middle and half their width
                // (about 0.6 of a letter's height per letter; a letter is about `gap` tall).
                std::vector<std::pair<sketch::Point2, double>> used;
                auto overlaps = [&](sketch::Point2 at, double half) {
                    return std::ranges::any_of(used, [&](const auto& u) {
                        return std::abs(u.first.x - at.x) < u.second + half &&
                               std::abs(u.first.y - at.y) < gap * 0.9;
                    });
                };
                const auto* design = ctx_.documents().find(ctx_.documents().active());
                const auto unit = design ? design->lengthUnit() : LengthUnit::Millimeter;
                for (const auto& c : whole.constraints) {
                    const auto text = sketcher::labelText(c, unit);
                    const auto spot = sketcher::labelPlace(c, whole, gap, middle);
                    if (text.empty() || !spot) continue;
                    const double half = 0.3 * gap * static_cast<double>(QString::fromStdString(text).size());
                    // A label where another one is moves on: further out above and below
                    // a line, downwards beside one (labels are wider than they're tall).
                    const bool sideways = std::abs(spot->away.x) > std::abs(spot->away.y);
                    const sketch::Point2 move = sideways
                                                    ? sketch::Point2{0, -gap}
                                                    : sketch::Point2{spot->away.x * gap, spot->away.y * gap};
                    auto place = spot->at;
                    if (sideways) place.x += spot->away.x * half;  // its near edge at the gap, not its middle
                    for (int tries = 0; tries < 12 && overlaps(place, half); ++tries) {
                        place = {place.x + move.x, place.y + move.y};
                    }
                    used.emplace_back(place, half);
                    labels_.emplace_back(c.id, place);
                    const Item item{{}, std::nullopt, c.id};
                    const bool conflict = last && listed(last->conflicting, c.id);
                    Overlay& into = std::ranges::find(picked_, item) != picked_.end() ? pickedLabels
                                    : hoverItem_ == item && !drag_                    ? hoverLabels
                                    : conflict                                        ? conflictLabels
                                    : last && listed(last->redundant, c.id)           ? redundantLabels
                                                                                      : labels;
                    into.points.push_back(model::toDesign(step.plane(), place));
                    into.texts.push_back(text);
                    if (conflict) {
                        for (const auto& e : entities) {
                            const bool on = std::ranges::find(c.entities, e.id) != c.entities.end() ||
                                            std::ranges::any_of(
                                                c.points, [&](const auto& p) { return p.entity == e.id; });
                            if (on) lineInto(conflictLines, e);
                        }
                    }
                }
            }
        }
        // Redrawn in this order every time, so what's picked is always on top.
        const std::pair<const char*, Overlay*> layers[] = {{"sketches", &others},
                                                           {"edit.helpers", &helpers},
                                                           {"edit.lines", &freeLines},
                                                           {"edit.fixed", &fixedLines},
                                                           {"edit.points", &freePoints},
                                                           {"edit.fixed_points", &fixedPoints},
                                                           {"edit.open_ends", &openEnds},
                                                           {"edit.hover", &hoverLines},
                                                           {"edit.hover_points", &hoverPoints},
                                                           {"edit.picked", &pickedLines},
                                                           {"edit.picked_points", &pickedPoints},
                                                           {"edit.conflict", &conflictLines},
                                                           {"edit.labels", &labels},
                                                           {"edit.labels_repeated", &redundantLabels},
                                                           {"edit.labels_conflict", &conflictLabels},
                                                           {"edit.labels_hover", &hoverLabels},
                                                           {"edit.labels_picked", &pickedLabels}};
        for (const auto& [name, overlay] : layers) overlays.remove(name);
        for (const auto& [name, overlay] : layers) show(overlays, name, std::move(*overlay));
        drawPreview();
    }

    // --- The mouse (IViewTool) -------------------------------------------

    void moved(const viewer::ToolPoint& point) override {
        if (tool_ == Tool::Select) {
            const auto item = itemAt(point);
            if (item != hoverItem_) {
                hoverItem_ = item;
                draw();
            }
            return;
        }
        hover_ = where(point);
        hoverPixel_ = point.pixel;
        drawPreview();
    }

    void clicked(const viewer::ToolPoint& point) override {
        if (!editing()) return;
        if (tool_ == Tool::Select) {
            pickAt(point);
            return;
        }
        const Pick here = where(point);
        hover_ = here;
        switch (tool_) {
            case Tool::Select:
                break;
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

    // Select: dragging a point moves it; dragging a line moves the whole line;
    // dragging the edge of a circle or arc changes its size. The solver keeps
    // every constraint true while it follows the mouse.
    void dragged(const viewer::ToolPoint& from, const viewer::ToolPoint& to) override {
        if (!editing() || tool_ != Tool::Select || dragCancelled_) return;
        const auto* step = sketchStep(editing_);
        auto* solver = ctx_.documents().solver();
        if (!step || !solver) return;
        if (!drag_) {
            const auto item = itemAt(from);
            if (!item || item->isConstraint()) return;  // nothing under the mouse to drag
            drag_ = Drag{*item, sketch::Sketch{current(*step), step->sketch().constraints}};
            if (std::ranges::find(picked_, *item) == picked_.end()) picked_ = {*item};
            ctx_.publish(plugin::ActionsChanged{});
        }
        const auto plane = step->plane();
        const auto a = model::toSketch(plane, from.at);
        const auto b = model::toSketch(plane, to.at);
        const sketch::Point2 delta{b.x - a.x, b.y - a.y};
        const auto& start = drag_->start;
        std::vector<sketch::Pull> pulls;
        auto movePoint = [&](sketch::PointRef ref) {
            if (const auto p = sketch::pointOf(start, ref))
                pulls.push_back({ref, {p->x + delta.x, p->y + delta.y}});
        };
        const Item& item = drag_->item;
        const auto* e = start.find(item.entity);
        if (!e) return;
        // A dragged point dropped within 8 pixels of another point (not one of its
        // own entity's) lands exactly on it, and they're joined with a Coincident.
        std::optional<sketch::PointRef> dragging;
        if (item.point) dragging = sketch::PointRef{item.entity, *item.point};
        if (!item.point && std::holds_alternative<sketch::PointShape>(e->shape))
            dragging = sketch::PointRef{item.entity, sketch::PointRole::Self};
        drag_->joinTo.reset();
        hover_.reset();
        if (dragging) {
            if (const auto p = sketch::pointOf(start, *dragging)) {
                const sketch::Point2 there{p->x + delta.x, p->y + delta.y};
                double best = kSnapPixels * to.pixel;
                for (const auto& [ref, q] : sketchPoints(start.entities)) {
                    if (ref.entity == item.entity) continue;
                    if (distance(there, q) < best) {
                        best = distance(there, q);
                        drag_->joinTo = ref;
                    }
                }
            }
        }
        if (drag_->joinTo) {
            const auto onto = *sketch::pointOf(start, *drag_->joinTo);
            pulls.push_back({*dragging, onto});
            hover_ = Pick{onto, drag_->joinTo};  // the orange snap dot shows it
        } else if (item.point) {
            movePoint({item.entity, *item.point});
        } else if (std::holds_alternative<sketch::PointShape>(e->shape)) {
            movePoint({item.entity, sketch::PointRole::Self});
        } else if (std::holds_alternative<sketch::LineShape>(e->shape)) {
            movePoint({item.entity, sketch::PointRole::Start});
            movePoint({item.entity, sketch::PointRole::End});
        } else {
            pulls.push_back({{item.entity, sketch::PointRole::Center}, b, true});  // the edge: its size
        }
        const auto r = solver->drag(start, pulls);
        if (r.ok()) drag_->now = r.entities;
        draw();
    }

    void dropped(const viewer::ToolPoint& from, const viewer::ToolPoint& to) override {
        if (dragCancelled_) {
            dragCancelled_ = false;
            return;
        }
        if (!drag_) return;
        dragged(from, to);
        const auto d = std::move(*drag_);
        drag_.reset();
        hover_.reset();
        if (!d.now || (moved(d.start.entities, *d.now) == 0 && !d.joinTo)) {
            draw();
            return;
        }
        const auto moved = *d.now;
        auto pointName = [&](const sketch::PointRef& r) {
            const auto* x = d.start.find(r.entity);
            if (!x) return std::string("a point");
            return r.role == sketch::PointRole::Self ? x->name : x->name + "'s " + roleName(r.role);
        };
        const auto* e = d.start.find(d.item.entity);
        const std::string what = !e ? std::string("sketch")
                                 : d.item.point && d.item.point != sketch::PointRole::Self
                                     ? e->name + "'s " + roleName(*d.item.point)
                                     : e->name;
        // The point that was dragged, if it's being joined.
        std::optional<sketch::PointRef> joining;
        if (d.joinTo) {
            joining = d.item.point ? sketch::PointRef{d.item.entity, *d.item.point}
                                   : sketch::PointRef{d.item.entity, sketch::PointRole::Self};
        }
        const auto joinTo = d.joinTo;
        const std::string label =
            joining ? "Join " + pointName(*joining) + " to " + pointName(*joinTo) : "Move " + what;
        run([moved, joining, joinTo, label](sketch::Sketch& s, std::string& undoLabel) -> Diagnostics {
            for (auto& entity : s.entities) {
                for (const auto& m : moved) {
                    if (m.id == entity.id) entity.shape = m.shape;
                }
            }
            if (joining) {
                sketch::Constraint c;
                c.kind = sketch::ConstraintKind::Coincident;
                c.points = {*joining, *joinTo};
                const bool there = std::ranges::any_of(s.constraints, [&](const sketch::Constraint& x) {
                    return sketcher::sameConstraint(x, c);
                });
                if (!there) {
                    c.id = ConstraintId::generate();
                    c.name = model::uniqueSketchName(s, "Coincident");
                    s.constraints.push_back(std::move(c));
                }
            }
            undoLabel = label;
            return {};
        });
        hint();
    }

    void cancelled() override {
        if (drag_) {  // Escape while dragging: put it back, and ignore the drop that follows
            drag_.reset();
            hover_.reset();
            dragCancelled_ = true;
            draw();
            return;
        }
        if (!pending_.empty()) {
            pending_.clear();
            drawPreview();
            hint();
            return;
        }
        if (tool_ != Tool::Select) {
            setTool(Tool::Select);
            return;
        }
        if (!picked_.empty()) {
            picked_.clear();
            draw();
            hint();
            refreshPanels();
            ctx_.publish(plugin::ActionsChanged{});
            return;
        }
        finish();
    }

    // Stops editing if the sketch went away (deleted, undone, another design),
    // and lets go of picked parts that went away.
    void checkStillThere() {
        if (editing() && (ctx_.documents().active() != doc_ || !sketchStep(editing_))) finish();
        // Constraints that can't all be true: say which, in plain words (they're red too).
        if (const auto* step = sketchStep(editing_)) {
            const auto* last = lastResult(*step);
            if (last && last->status == sketch::SolveStatus::Conflicting && !last->issues.empty()) {
                if (auto* ui = ctx_.service<ui::UiRegistry>()) ui->showProblems(last->issues);
            }
        }
        if (const auto* step = sketchStep(editing_)) {
            const auto before = picked_.size();
            auto gone = [&](const Item& i) {
                return i.isConstraint() ? !step->sketch().find(i.constraint) : !step->sketch().find(i.entity);
            };
            std::erase_if(picked_, gone);
            if (hoverItem_ && gone(*hoverItem_)) hoverItem_.reset();
            if (picked_.size() != before) ctx_.publish(plugin::ActionsChanged{});
        }
        draw();
        refreshPanels();
    }

private:
    // Blue for what can still move, black for what's fully constrained (as in
    // other CAD programs); orange for what's picked, light orange under the mouse.
    static constexpr plugin::OverlayColor kFree{9, 105, 218};
    static constexpr plugin::OverlayColor kFixed{31, 35, 40};
    static constexpr plugin::OverlayColor kPicked{230, 110, 0};
    static constexpr plugin::OverlayColor kHover{245, 170, 80};
    static constexpr plugin::OverlayColor kLabel{55, 65, 81};       // constraint labels
    static constexpr plugin::OverlayColor kConflict{207, 34, 46};   // constraints that can't all be true
    static constexpr plugin::OverlayColor kRedundant{176, 120, 0};  // constraints that repeat others
    static constexpr double kEdgePixels = 6;    // how close the mouse must be to a line to pick it
    static constexpr double kLabelPixels = 12;  // ... and to a constraint's label
    static constexpr double kAutoPixels = 6;    // how close to level or upright a line snaps to it

    struct Drag {
        Item item;                                         // what's dragged
        sketch::Sketch start;                              // the sketch when the drag started
        std::optional<std::vector<sketch::Entity>> now{};  // where the solver has it now
        std::optional<sketch::PointRef> joinTo{};          // the point it will be joined to, if dropped now
    };

    // What's picked, sorted for the constraint rules. A picked constraint
    // label counts as something no rule takes.
    sketcher::Picks picks(const model::SketchFeature& step) const {
        sketcher::Picks out;
        const sketch::Sketch s{current(step), {}};
        for (const auto& item : picked_) {
            const auto* e = item.isConstraint() ? nullptr : s.find(item.entity);
            if (!e) {
                ++out.other;
                continue;
            }
            if (item.point) {
                out.points.push_back({item.entity, *item.point});
            } else if (std::holds_alternative<sketch::PointShape>(e->shape)) {
                out.points.push_back({item.entity, sketch::PointRole::Self});
            } else if (std::holds_alternative<sketch::LineShape>(e->shape)) {
                out.lines.push_back(item.entity);
            } else {
                out.rounds.push_back(item.entity);
            }
        }
        return out;
    }

    void reset() {
        pending_.clear();
        hover_.reset();
        hoverItem_.reset();
        picked_.clear();
        drag_.reset();
        dragCancelled_ = false;
    }

    static std::string roleName(sketch::PointRole role) {
        switch (role) {
            case sketch::PointRole::Start:
                return "start";
            case sketch::PointRole::End:
                return "end";
            case sketch::PointRole::Center:
                return "center";
            case sketch::PointRole::Self:
                break;
        }
        return "point";
    }

    // How many entities moved (by more than a millionth of a mm).
    static std::size_t moved(const std::vector<sketch::Entity>& a, const std::vector<sketch::Entity>& b) {
        std::size_t n = 0;
        const sketch::Sketch sb{b, {}};
        for (const auto& e : a) {
            for (const auto& [ref, p] : sketchPoints({e})) {
                const auto q = sketch::pointOf(sb, ref);
                if (q && distance(p, *q) > 1e-6) {
                    ++n;
                    break;
                }
            }
            const auto* f = sb.find(e.id);
            auto radius = [](const sketch::Shape& s) {
                if (const auto* c = std::get_if<sketch::CircleShape>(&s)) return c->radius;
                if (const auto* r = std::get_if<sketch::ArcShape>(&s)) return r->radius;
                return 0.0;
            };
            if (f && std::abs(radius(e.shape) - radius(f->shape)) > 1e-6) ++n;
        }
        return n;
    }

    const model::SketchFeature* sketchStep(const FeatureId& id) const {
        const auto* doc = ctx_.documents().find(ctx_.documents().active());
        const auto* f = doc ? doc->find(id) : nullptr;
        return f && f->type() == model::FeatureType::Sketch ? static_cast<const model::SketchFeature*>(f)
                                                            : nullptr;
    }

    // The last solve of a sketch step, however it ended (for conflicts).
    const sketch::SolveResult* lastResult(const model::SketchFeature& step) const {
        const auto* report = ctx_.documents().lastRebuild(ctx_.documents().active());
        const auto* r = report ? report->result(step.id()) : nullptr;
        return r && r->sketch ? &*r->sketch : nullptr;
    }

    // How far labels sit from what they're on (a little, for the sketch's size),
    // and the middle of the sketch, so labels go on the outside.
    static std::pair<double, sketch::Point2> labelSpacing(const std::vector<sketch::Entity>& entities) {
        double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
        auto grow = [&](double x, double y, double r) {
            x0 = std::min(x0, x - r), y0 = std::min(y0, y - r), x1 = std::max(x1, x + r),
            y1 = std::max(y1, y + r);
        };
        for (const auto& [ref, p] : sketchPoints(entities)) grow(p.x, p.y, 0);
        for (const auto& e : entities) {
            if (const auto* c = std::get_if<sketch::CircleShape>(&e.shape))
                grow(c->center.x, c->center.y, c->radius);
        }
        if (x1 < x0) return {1, {0, 0}};
        const double size = std::hypot(x1 - x0, y1 - y0);
        return {std::clamp(size * 0.06, 0.5, 20.0), {(x0 + x1) / 2, (y0 + y1) / 2}};
    }

    const sketch::SolveResult* solveResult(const model::SketchFeature& step) const {
        const auto* report = ctx_.documents().lastRebuild(ctx_.documents().active());
        const auto* r = report ? report->result(step.id()) : nullptr;
        return r && r->sketch && r->sketch->ok() ? &*r->sketch : nullptr;
    }

    // A sketch as the student sees it: solved, if it solved; as drawn otherwise.
    std::vector<sketch::Entity> shown(const model::SketchFeature& step) const {
        if (const auto* r = solveResult(step)) return r->entities;
        return step.sketch().entities;
    }

    // The edited sketch as it is right now: following the mouse while dragging.
    std::vector<sketch::Entity> current(const model::SketchFeature& step) const {
        if (drag_ && drag_->now && step.id() == editing_) return *drag_->now;
        return shown(step);
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

    // The part of the edited sketch under the mouse: a point within 8 pixels,
    // else an edge within 6, else a constraint's label within 12.
    std::optional<Item> itemAt(const viewer::ToolPoint& point) const {
        const auto* step = sketchStep(editing_);
        if (!step) return std::nullopt;
        const auto at = model::toSketch(step->plane(), point.at);
        const auto entities = current(*step);
        std::optional<Item> best;
        double bestDistance = kSnapPixels * point.pixel;
        for (const auto& [ref, p] : sketchPoints(entities)) {
            const double d = distance(at, p);
            if (d < bestDistance || (!best && d <= bestDistance)) {  // two points in one place: the first
                // A point entity is picked whole; other points by their role.
                best = ref.role == sketch::PointRole::Self ? Item{ref.entity, std::nullopt}
                                                           : Item{ref.entity, ref.role};
                bestDistance = d;
            }
        }
        if (best) return best;
        bestDistance = kEdgePixels * point.pixel;
        for (const auto& e : entities) {
            const auto d = edgeDistance(e.shape, at);
            if (d && (*d < bestDistance || (!best && *d <= bestDistance))) {
                best = Item{e.id, std::nullopt};
                bestDistance = *d;
            }
        }
        if (best) return best;
        // Then constraint labels, beside what they're on (points and lines win when
        // the mouse is right on them).
        bestDistance = kLabelPixels * point.pixel;
        for (const auto& [id, p] : labels_) {
            const double d = distance(at, p);
            if (d < bestDistance) {
                best = Item{{}, std::nullopt, id};
                bestDistance = d;
            }
        }
        return best;
    }

    // A click in Select: picks what's under the mouse (Ctrl or Shift adds or
    // removes it); a click on nothing lets go of everything.
    void pickAt(const viewer::ToolPoint& point) {
        const auto item = itemAt(point);
        if (!item) {
            if (!point.add) picked_.clear();
        } else if (point.add) {
            if (const auto it = std::ranges::find(picked_, *item); it != picked_.end())
                picked_.erase(it);
            else
                picked_.push_back(*item);
        } else {
            picked_ = {*item};
        }
        draw();
        hint();
        refreshPanels();
        ctx_.publish(plugin::ActionsChanged{});  // Delete turns on or off
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
        // Onto a line, circle or arc (and On is added), when that's turned on.
        if (autoConstraints()) {
            std::optional<std::pair<EntityId, sketch::Point2>> edge;
            double edgeDistance = kSnapPixels * point.pixel;
            for (const auto& e : shown(*step)) {
                if (e.construction) continue;
                const auto near = nearestOnEdge(e.shape, at);
                if (near && distance(at, *near) < edgeDistance) {
                    edge.emplace(e.id, *near);
                    edgeDistance = distance(at, *near);
                }
            }
            if (edge) return {edge->second, std::nullopt, edge->first};
        }
        if (const auto* grid = ctx_.service<grid::IGrid>()) {
            return {model::toSketch(plane, grid->snap(point.at, point.pane)), std::nullopt};
        }
        return {at, std::nullopt};
    }

    // Constraints added while drawing: on unless the student turned them off.
    bool autoConstraints() const { return !ctx_.settings().flag("auto_constraints_off"); }

    // Where the mouse lands for the tool: pick(), and for a line's end, kept
    // level or upright when it's within 6 pixels of that, or running on smoothly
    // from the arc it starts at.
    Pick where(const viewer::ToolPoint& point) const {
        Pick here = pick(point);
        if (tool_ != Tool::Line || pending_.empty() || here.ref || here.on || !autoConstraints()) return here;
        const auto* step = sketchStep(editing_);
        if (!step) return here;
        const auto& from = pending_[0];
        const double close = kAutoPixels * point.pixel;
        const sketch::Point2 d{here.at.x - from.at.x, here.at.y - from.at.y};
        const double length = std::hypot(d.x, d.y);
        if (length <= close) return here;
        // Off the end of an arc, in nearly the arc's direction: exactly tangent.
        if (from.ref &&
            (from.ref->role == sketch::PointRole::Start || from.ref->role == sketch::PointRole::End)) {
            const sketch::Sketch s{shown(*step), {}};
            const auto* e = s.find(from.ref->entity);
            if (const auto* arc = e ? std::get_if<sketch::ArcShape>(&e->shape) : nullptr) {
                const bool atEnd = from.ref->role == sketch::PointRole::End;
                const double t = atEnd ? arc->endAngle : arc->startAngle;
                // The way the arc is heading as it leaves this end.
                const sketch::Point2 dir = atEnd ? sketch::Point2{-std::sin(t), std::cos(t)}
                                                 : sketch::Point2{std::sin(t), -std::cos(t)};
                const double along = d.x * dir.x + d.y * dir.y;
                if (along > 0 && std::abs(d.x * dir.y - d.y * dir.x) <= close) {
                    here.at = {from.at.x + dir.x * along, from.at.y + dir.y * along};
                    here.align = sketch::ConstraintKind::Tangent;
                    here.alignWith = e->id;
                    return here;
                }
            }
        }
        if (std::abs(d.y) <= close) {
            here.at.y = from.at.y;
            here.align = sketch::ConstraintKind::Horizontal;
        } else if (std::abs(d.x) <= close) {
            here.at.x = from.at.x;
            here.align = sketch::ConstraintKind::Vertical;
        }
        return here;
    }

    // The point of an edge nearest to p, if p is beside it (lines: between the
    // ends; arcs: within their sweep).
    static std::optional<sketch::Point2> nearestOnEdge(const sketch::Shape& shape, sketch::Point2 p) {
        if (const auto* l = std::get_if<sketch::LineShape>(&shape)) {
            const double dx = l->end.x - l->start.x, dy = l->end.y - l->start.y, len2 = dx * dx + dy * dy;
            if (len2 <= 0) return std::nullopt;
            const double t = ((p.x - l->start.x) * dx + (p.y - l->start.y) * dy) / len2;
            if (t < 0 || t > 1) return std::nullopt;
            return sketch::Point2{l->start.x + t * dx, l->start.y + t * dy};
        }
        auto toCircle = [&](sketch::Point2 c, double r) -> std::optional<sketch::Point2> {
            const double d = distance(c, p);
            if (d <= 1e-12) return std::nullopt;
            return sketch::Point2{c.x + (p.x - c.x) * r / d, c.y + (p.y - c.y) * r / d};
        };
        if (const auto* c = std::get_if<sketch::CircleShape>(&shape)) return toCircle(c->center, c->radius);
        if (const auto* a = std::get_if<sketch::ArcShape>(&shape)) {
            double t = std::atan2(p.y - a->center.y, p.x - a->center.x);
            while (t < a->startAngle) t += 2 * kPi;
            while (t >= a->startAngle + 2 * kPi) t -= 2 * kPi;
            if (t > a->endAngle) return std::nullopt;
            return toCircle(a->center, a->radius);
        }
        return std::nullopt;
    }

    // The shape being drawn, following the mouse, and the point it would snap to.
    void drawPreview() {
        auto& overlays = ctx_.overlays();
        plugin::Overlay preview{.shape = plugin::OverlayShape::Lines, .color = {230, 120, 20}, .size = 1.5};
        plugin::Overlay snap{.shape = plugin::OverlayShape::Points, .color = {230, 120, 20}, .size = 11};
        // What will be added with the click, beside the mouse: "H", "V", "T" or "On".
        plugin::Overlay hintLabel{.shape = plugin::OverlayShape::Labels, .color = {230, 120, 20}, .size = 13};
        const auto* step = sketchStep(editing_);
        if (step && hover_ && (tool_ != Tool::Select || drag_)) {
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
                    case Tool::Select:
                    case Tool::Point:
                        break;
                }
            }
            if (h.ref || h.on) snap.points.push_back(model::toDesign(plane, h.at));
            const char* word = h.on                                            ? "On"
                               : h.align == sketch::ConstraintKind::Horizontal ? "H"
                               : h.align == sketch::ConstraintKind::Vertical   ? "V"
                               : h.align == sketch::ConstraintKind::Tangent    ? "T"
                                                                               : nullptr;
            if (word) {
                const double off = 14 * hoverPixel_;  // up and to the right of the mouse
                hintLabel.points.push_back(model::toDesign(plane, {h.at.x + off, h.at.y + off}));
                hintLabel.texts.emplace_back(word);
            }
        }
        show(overlays, "edit.preview", std::move(preview));
        show(overlays, "edit.snap", std::move(snap));
        show(overlays, "edit.hint", std::move(hintLabel));
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

    // --- Changing the sketch: one undo step each ---------------------------

    // Runs a change to the edited sketch as a command; shows its problems.
    bool run(model::EditSketchCommand::Change change) {
        auto problems = ctx_.documents().execute(
            doc_, std::make_unique<model::EditSketchCommand>(editing_, std::move(change)));
        if (problems.empty()) return true;
        if (auto* ui = ctx_.service<ui::UiRegistry>()) ui->showProblems(problems);
        return false;
    }

    // Where each of a new shape's points was picked: what it snapped onto.
    using Join = std::pair<sketch::PointRole, Pick>;

    // Adds a shape as one undo step, with the constraints its picks call for: a
    // Coincident for a point it snapped onto, On for an edge, and Horizontal,
    // Vertical or Tangent for a line's end that was kept that way.
    std::optional<EntityId> add(const std::string& base, sketch::Shape shape, std::vector<Join> joins) {
        const EntityId id = EntityId::generate();
        const bool isLine = std::holds_alternative<sketch::LineShape>(shape);
        auto change = [id, base, shape, joins, isLine](sketch::Sketch& s, std::string& label) -> Diagnostics {
            const std::string name = model::uniqueSketchName(s, base);
            s.entities.push_back({id, name, shape, false});
            auto addConstraint = [&](sketch::ConstraintKind kind, std::vector<sketch::PointRef> points,
                                     std::vector<EntityId> entities) {
                sketch::Constraint c;
                c.id = ConstraintId::generate();
                c.kind = kind;
                c.points = std::move(points);
                c.entities = std::move(entities);
                c.name = model::uniqueSketchName(s, sketcher::constraintBase(kind));
                s.constraints.push_back(std::move(c));
            };
            for (const auto& [role, pick] : joins) {
                const sketch::PointRef mine{id, role};
                if (pick.ref) {
                    addConstraint(sketch::ConstraintKind::Coincident, {mine, *pick.ref}, {});
                } else if (pick.on) {
                    const auto* e = s.find(*pick.on);
                    if (!e) continue;
                    const bool line = std::holds_alternative<sketch::LineShape>(e->shape);
                    addConstraint(
                        line ? sketch::ConstraintKind::PointOnLine : sketch::ConstraintKind::PointOnCircle,
                        {mine}, {*pick.on});
                }
                if (isLine && pick.align) {
                    if (*pick.align == sketch::ConstraintKind::Tangent && pick.alignWith)
                        addConstraint(sketch::ConstraintKind::Tangent, {}, {id, *pick.alignWith});
                    else if (*pick.align != sketch::ConstraintKind::Tangent)
                        addConstraint(*pick.align, {}, {id});
                }
            }
            label = "Add " + name;
            return {};
        };
        if (!run(std::move(change))) return std::nullopt;
        return id;
    }

    std::optional<EntityId> addLine(const Pick& from, const Pick& to) {
        return add("Line", sketch::LineShape{from.at, to.at},
                   {{sketch::PointRole::Start, from}, {sketch::PointRole::End, to}});
    }

    void addCircle(const Pick& center, double radius) {
        add("Circle", sketch::CircleShape{center.at, radius}, {{sketch::PointRole::Center, center}});
    }

    void addArc(const Pick& center, const Pick& start, const Pick& end) {
        if (const auto arc = arcFrom(center.at, start.at, end.at)) {
            add("Arc", *arc,
                {{sketch::PointRole::Center, center},
                 {sketch::PointRole::Start, start},
                 {sketch::PointRole::End, end}});
        }
    }

    void addPoint(const Pick& at) {
        add("Point", sketch::PointShape{at.at}, {{sketch::PointRole::Self, at}});
    }

    // --- Messages ----------------------------------------------------------

    void hint() {
        if (!editing()) return;
        const auto* step = sketchStep(editing_);
        const std::string name = step ? step->name() : std::string("the sketch");
        if (tool_ != Tool::Select) {
            message("Drawing in " + name + ". " + toolHint(tool_, pending_.size()));
            return;
        }
        if (picked_.empty()) {
            message("Editing " + name + ". " + toolHint(tool_, 0));
            return;
        }
        std::string what;
        const sketch::Sketch* s = step ? &step->sketch() : nullptr;
        for (const auto& item : picked_) {
            if (item.isConstraint()) {
                if (const auto* c = s ? s->find(item.constraint) : nullptr)
                    what += (what.empty() ? "" : ", ") + c->name;
                continue;
            }
            const auto* e = s ? s->find(item.entity) : nullptr;
            if (!e) continue;
            what +=
                (what.empty() ? "" : ", ") + (item.point ? e->name + "'s " + roleName(*item.point) : e->name);
        }
        const bool labelsOnly =
            std::ranges::all_of(picked_, [](const Item& item) { return item.isConstraint(); });
        message("Picked " + what +
                (labelsOnly
                     ? ". Delete deletes it; change a number in the Sketch panel; Esc lets go."
                     : ". The buttons show what you can add; drag to move; Delete deletes; Esc lets go."));
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
    std::vector<Pick> pending_;                                    // the clicks of the shape being drawn
    std::optional<Pick> hover_;                                    // where the mouse is (drawing tools)
    double hoverPixel_ = 0.1;                                      // how many mm a screen pixel covers there
    std::optional<Item> hoverItem_;                                // what's under the mouse (Select)
    std::vector<Item> picked_;                                     // what's picked (Select), in picking order
    std::optional<Drag> drag_;                                     // what's being dragged
    std::vector<std::pair<ConstraintId, sketch::Point2>> labels_;  // where the edited sketch's labels are
    ConstraintId justAdded_{};
    std::vector<QPointer<sketcher::SketchPanel>> panels_;  // a new dimension, for the Sketch panel
    bool dragCancelled_ = false;                           // Escape during a drag: ignore its drop
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
        sketcher->begin(id, Tool::Line);  // a new sketch starts with lines
    };
    ctx.addAction(ActionSpec{.id = "kh.sketcher.new_xy",
                             .text = "New sketch on XY (the ground)",
                             .menu = "Sketch",
                             .shortcut = "Ctrl+Shift+K",
                             .toolbar = true,
                             .buttonText = "New sketch",
                             .icon = std::string(icons::sketcher::new_sketch_svg),
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
    // The one sketch picked in the active design, if that's what's picked.
    auto pickedSketch = [&ctx]() -> std::optional<FeatureId> {
        const auto& sel = ctx.selection();
        const DocId active = ctx.documents().active();
        const auto* doc = ctx.documents().find(active);
        if (!doc || sel.doc() != active || sel.features().size() != 1) return std::nullopt;
        const auto* f = doc->find(sel.features()[0]);
        if (!f || f->type() != model::FeatureType::Sketch) return std::nullopt;
        return f->id();
    };
    ctx.addAction(ActionSpec{
        .id = "kh.sketcher.edit",
        .text = "Edit sketch",
        .menu = "Sketch",
        .shortcut = "Ctrl+K",
        .toolbar = true,
        .icon = std::string(icons::sketcher::edit_sketch_svg),
        .tip = "Draw in the picked sketch.",
        .run =
            [&ctx, sketcher, pickedSketch] {
                if (const auto id = pickedSketch()) {
                    sketcher->begin(*id, Tool::Select);  // a sketch you have starts with picking
                } else if (auto* ui = ctx.service<ui::UiRegistry>()) {
                    ui->showProblems({{"sketch.pick",
                                       Severity::Error,
                                       "Pick one sketch in the feature tree first, like "
                                       "Sketch1.",
                                       {}}});
                }
            },
        // Only with one sketch picked, and not while already editing one.
        .enabled = [sketcher, pickedSketch] { return !sketcher->editing() && pickedSketch().has_value(); }});
    ctx.addAction(ActionSpec{.id = "kh.sketcher.finish",
                             .text = "Finish sketch",
                             .menu = "Sketch",
                             .toolbar = true,
                             .toolbarMode = std::string(kEditingMode),
                             .icon = std::string(icons::sketcher::finish_sketch_svg),
                             .tip = "Stop drawing. Clicks in the viewer pick and turn the view again.",
                             .run = [sketcher] { sketcher->finish(); },
                             .enabled = [sketcher] { return sketcher->editing(); }});
    ctx.addAction(
        ActionSpec{.id = "kh.sketcher.select",
                   .text = "Select",
                   .menu = "Sketch/Draw",
                   .toolbar = true,
                   .toolbarMode = std::string(kEditingMode),
                   .icon = std::string(icons::sketcher::select_svg),
                   .tip = "Pick lines and points, and drag them to move them.",
                   .run = [sketcher] { sketcher->setTool(Tool::Select); },
                   .checked = [sketcher] { return sketcher->editing() && sketcher->tool() == Tool::Select; },
                   .enabled = [sketcher] { return sketcher->editing(); }});
    ctx.addAction(
        ActionSpec{.id = "kh.sketcher.delete",
                   .text = "Delete picked",
                   .menu = "Sketch",
                   .shortcut = "Delete",
                   .toolbarMode = std::string(kEditingMode),  // while editing, Delete is this
                   .tip = "Delete the picked lines, circles, arcs and points, and their constraints.",
                   .run = [sketcher] { sketcher->deletePicked(); },
                   .enabled = [sketcher] { return sketcher->editing() && !sketcher->picked().empty(); }});
    struct ToolItem {
        const char* id;
        const char* text;
        Tool tool;
        const char* tip;
        std::string_view icon;
    };
    for (const auto& t :
         {ToolItem{"kh.sketcher.line", "Line", Tool::Line,
                   "Draw lines: click where each one starts and ends. They join up as you go.",
                   icons::sketcher::line_svg},
          ToolItem{"kh.sketcher.circle", "Circle", Tool::Circle,
                   "Draw a circle: click its center, then its edge.", icons::sketcher::circle_svg},
          ToolItem{"kh.sketcher.arc", "Arc", Tool::Arc,
                   "Draw an arc: click the center, where it starts, then where it ends.",
                   icons::sketcher::arc_svg},
          ToolItem{"kh.sketcher.point", "Point", Tool::Point, "Draw a point: click where it goes.",
                   icons::sketcher::point_svg}}) {
        const Tool tool = t.tool;
        ctx.addAction(ActionSpec{
            .id = t.id,
            .text = t.text,
            .menu = "Sketch/Draw",
            .toolbar = true,
            .toolbarMode = std::string(kEditingMode),
            .icon = std::string(t.icon),
            .tip = t.tip,
            .run = [sketcher, tool] { sketcher->setTool(tool); },
            .checked = [sketcher, tool] { return sketcher->editing() && sketcher->tool() == tool; },
            .enabled = [sketcher] { return sketcher->editing(); }});
    }

    // Constraints added while drawing (On, Horizontal, Vertical, Tangent): on by default.
    ctx.addAction(ActionSpec{.id = "kh.sketcher.auto",
                             .text = "Add constraints while drawing",
                             .menu = "Sketch",
                             .tip = "Snap onto lines and keep lines level or upright as you draw, adding the "
                                    "constraints that keep them that way. Turn it off to draw freely.",
                             .run =
                                 [&ctx] {
                                     ctx.settings().set("auto_constraints_off",
                                                        !ctx.settings().flag("auto_constraints_off"));
                                 },
                             .checked = [&ctx] { return !ctx.settings().flag("auto_constraints_off"); }});

    // The Sketch panel: the edited sketch's constraints, with numbers to type.
    if (auto* ui = ctx.service<ui::UiRegistry>()) {
        ui->addPanel({"kh.sketcher.panel", QObject::tr("Sketch"), ui::PanelArea::Right,
                      [&ctx, sketcher] {
                          auto* panel = new sketcher::SketchPanel(
                              ctx,
                              {.editing = [sketcher] { return sketcher->editingStep(); },
                               .picked = [sketcher] { return sketcher->pickedConstraints(); },
                               .pick = [sketcher](const ConstraintId& id) { sketcher->pickConstraint(id); },
                               .takeJustAdded = [sketcher] { return sketcher->takeJustAdded(); }});
                          sketcher->addPanel(panel);
                          return panel;
                      },
                      false, true, true});  // starts closed; it opens over the view when a sketch is edited
    }

    // Constraints and dimensions: a button for each, shown only when it fits
    // what's picked, so the toolbar offers just what makes sense.
    auto iconFor = [](Rule rule) -> std::string_view {
        namespace i = icons::sketcher;
        switch (rule) {
            case Rule::Coincident:
                return i::coincident_svg;
            case Rule::Horizontal:
                return i::horizontal_svg;
            case Rule::Vertical:
                return i::vertical_svg;
            case Rule::Parallel:
                return i::parallel_svg;
            case Rule::Perpendicular:
                return i::perpendicular_svg;
            case Rule::Equal:
                return i::equal_svg;
            case Rule::Tangent:
                return i::tangent_svg;
            case Rule::On:
                return i::on_svg;
            case Rule::Midpoint:
                return i::midpoint_svg;
            case Rule::Fix:
                return i::fix_svg;
            case Rule::Dimension:
                return i::dimension_svg;
            case Rule::HorizontalDimension:
                return i::horizontal_dimension_svg;
            case Rule::VerticalDimension:
                return i::vertical_dimension_svg;
        }
        return {};
    };
    for (const Rule rule : sketcher::allRules()) {
        const auto info = sketcher::ruleInfo(rule);
        const bool dimension =
            rule == Rule::Dimension || rule == Rule::HorizontalDimension || rule == Rule::VerticalDimension;
        ctx.addAction(
            ActionSpec{.id = std::string("kh.sketcher.") + info.id,
                       .text = info.text,
                       .menu = dimension ? "Sketch/Dimension" : "Sketch/Constrain",
                       .toolbar = true,
                       .buttonText = rule == Rule::HorizontalDimension ? "Width"
                                     : rule == Rule::VerticalDimension ? "Height"
                                                                       : "",
                       .toolbarMode = std::string(kEditingMode),
                       .buttonWhenEnabled = true,
                       .icon = std::string(iconFor(rule)),
                       .tip = info.tip,
                       .run = [sketcher, rule] { sketcher->constrain(rule); },
                       .enabled = [sketcher, rule] { return sketcher->editing() && sketcher->fits(rule); }});
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
