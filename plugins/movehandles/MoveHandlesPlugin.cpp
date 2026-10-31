#include "kh/plugins/MoveHandles.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include "kh/model/Commands.h"
#include "kh/plugin/Events.h"

namespace kh::plugins {

namespace {

constexpr const char* kManifest = R"({
  "id": "kh.movehandles",
  "name": "Move handles",
  "version": "0.1.0",
  "description": "Arrows on the picked body in the 3D view: drag one to slide the body along x, y or z, or the square to slide it on the ground.",
  "requires": ["kh.modeling", "kh.viewer"],
  "tier": "ui"
})";

// Puts the handles on the picked body, and slides it as they're dragged.
class Handles final : public viewer::IHandleUser {
public:
    Handles(plugin::PluginContext& ctx, viewer::IViewer& viewer) : ctx_(ctx), viewer_(viewer) {}

    // No clearHandles() when it goes: that's only when Khan closes, and the
    // viewer may already be gone then.

    // Shows handles on the picked body (or the body being dragged), or none.
    void refresh() {
        const DocId docId = ctx_.documents().active();
        const auto* doc = ctx_.documents().find(docId);
        const auto* report = ctx_.documents().lastRebuild(docId);
        const auto* kernel = ctx_.documents().kernel();
        std::optional<FeatureId> body;
        if (dragging_) {
            body = target_;
        } else if (doc && ctx_.selection().doc() == docId && ctx_.selection().features().size() == 1) {
            body = ctx_.selection().features().front();
        }
        const kernel::ShapeHandle* shape = body && report ? report->shapeOf(*body) : nullptr;
        if (!doc || !kernel || !shape ||
            (!dragging_ && !model::SlideBodyCommand::check(*doc, *body).empty())) {
            viewer_.clearHandles(*this);
            return;
        }
        viewer_.setHandles(*this, moveHandles(kernel->bounds(*shape)));
    }

    void dragged(const std::string&, const kernel::Vec3& by) override {
        const DocId docId = ctx_.documents().active();
        const auto* doc = ctx_.documents().find(docId);
        if (!doc) return;
        if (!dragging_) {
            // The drag starts: it slides the picked body.
            if (ctx_.selection().features().size() != 1) return;
            dragging_ = true;
            started_ = false;
            target_ = ctx_.selection().features().front();
            moveId_ = FeatureId::generate();
            applied_ = {};
        }
        if (failed_) return;
        const auto snapped = snapSlide(by, doc->lengthUnit());
        const kernel::Vec3 step{snapped.x - applied_.x, snapped.y - applied_.y, snapped.z - applied_.z};
        if (step.x == 0 && step.y == 0 && step.z == 0) return;
        // The rest of the drag joins the first part into one undo step.
        auto problems = ctx_.documents().execute(
            docId, std::make_unique<model::SlideBodyCommand>(target_, step, started_, moveId_));
        if (!problems.empty()) {
            failed_ = true;  // say nothing more until the drag ends
            ctx_.log(problems.front().message);
            return;
        }
        started_ = true;
        applied_ = snapped;
        if (doc->find(moveId_) && target_ != moveId_) {
            // It added a Move step: the rest of the drag moves that, and it's what's picked now.
            target_ = moveId_;
            ctx_.selection().set(docId, {moveId_});
        }
        refresh();
    }

    void dropped(const std::string&, const kernel::Vec3&) override {
        dragging_ = failed_ = false;
        refresh();
    }

    void cancelled() override {
        if (dragging_ && started_) {
            const DocId docId = ctx_.documents().active();
            const FeatureId picked = target_ == moveId_ ? original_ : target_;
            ctx_.documents().undo(docId);  // the whole drag is one step
            if (ctx_.documents().find(docId)) ctx_.selection().set(docId, {picked});
        }
        dragging_ = failed_ = false;
        refresh();
    }

    // Remembers what was picked when a drag starts, for Escape.
    void rememberPick() {
        if (!dragging_ && ctx_.selection().features().size() == 1)
            original_ = ctx_.selection().features().front();
    }

private:
    plugin::PluginContext& ctx_;
    viewer::IViewer& viewer_;
    bool dragging_ = false;  // a drag has started
    bool started_ = false;   // it has changed the design
    bool failed_ = false;    // the design refused it
    FeatureId target_;       // the step being slid
    FeatureId original_;     // what was picked before the drag
    FeatureId moveId_;       // the Move step a slide may add
    kernel::Vec3 applied_{};
};

void registerMoveHandles(plugin::PluginContext& ctx) {
    auto* viewer = ctx.service<viewer::IViewer>();
    if (!viewer) {
        ctx.fail("Move handles need the 3D viewer (kh.viewer). Reinstall Khan to get it back.");
        return;
    }
    // Lives as long as the plugin: the listeners below hold it.
    auto handles = std::make_shared<Handles>(ctx, *viewer);
    auto refresh = [handles](const auto&) {
        handles->rememberPick();
        handles->refresh();
    };
    ctx.listen<plugin::SelectionChanged>(refresh);
    ctx.listen<plugin::RebuildFinished>(refresh);
    ctx.listen<plugin::ActiveDocumentChanged>(refresh);
    ctx.listen<plugin::DocumentClosed>(refresh);
}

}  // namespace

std::vector<viewer::Handle> moveHandles(const kernel::Box3& b) {
    const kernel::Vec3 middle{(b.min.x + b.max.x) / 2, (b.min.y + b.max.y) / 2, (b.min.z + b.max.z) / 2};
    std::vector<viewer::Handle> out;
    out.push_back({"x", viewer::HandleKind::Arrow, middle, {1, 0, 0}, kArrowPixels, 220, 50, 50});
    out.push_back({"y", viewer::HandleKind::Arrow, middle, {0, 1, 0}, kArrowPixels, 40, 170, 60});
    out.push_back({"z", viewer::HandleKind::Arrow, middle, {0, 0, 1}, kArrowPixels, 40, 100, 230});
    out.push_back({"xy", viewer::HandleKind::Square, middle, {0, 0, 1}, kSquarePixels, 190, 110, 220});
    return out;
}

kernel::Vec3 snapSlide(const kernel::Vec3& by, LengthUnit unit) {
    const double step = (unit == LengthUnit::Inch || unit == LengthUnit::Foot) ? 2.54 : 1.0;
    auto snap = [step](double v) {
        const double s = std::round(v / step) * step;
        return s == 0 ? 0.0 : s;  // never -0
    };
    return {snap(by.x), snap(by.y), snap(by.z)};
}

plugin::BuiltinPlugin moveHandlesPlugin() {
    return {kManifest, &registerMoveHandles};
}

}  // namespace kh::plugins
