#include "kh/plugins/Viewer.h"

#include <QAction>
#include <QActionGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSplitter>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "kh/model/Document.h"
#include "kh/plugin/Events.h"
#include "kh/ui/UiRegistry.h"
#include "kh/view/FlatView.h"
#include "kh/view/Graphics.h"
#include "kh/view/Layout.h"
#include "kh/view/ViewWidget.h"
#include "kh/viewer/Viewer.h"
#include "kh/viewoptions/ViewOptions.h"

namespace kh::plugins {

namespace {

constexpr const char* kManifest = R"({
  "id": "kh.viewer",
  "name": "3D viewer",
  "version": "0.1.0",
  "description": "The 3D view of a design: panes, standard views, the view cube, and what other plugins ask it to draw.",
  "requires": ["kh.modeling"],
  "uses": ["kh.viewoptions"],
  "tier": "ui"
})";

QString qs(std::string_view s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

// "Two side by side" -> "sidebyside", for object names.
QString slug(std::string_view name) {
    QString out;
    for (const QChar c : qs(name).toLower()) {
        if (c.isLetterOrNumber()) out += c;
    }
    return out;
}

class ViewerPanel;

kernel::Vec3 cross(const kernel::Vec3& a, const kernel::Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

view::DrawingPlane drawingPlane(const viewer::ToolPlane& p) {
    return {p.origin, cross(p.xAxis, p.yAxis)};
}

// The kh.viewer/1 service: one drawing tool for every pane of every viewer.
class ViewerService final : public viewer::IViewer {
public:
    void startTool(viewer::IViewTool& tool, const viewer::ToolPlane& plane) override;
    void stopTool(const viewer::IViewTool& tool) override;
    viewer::IViewTool* tool() const override { return tool_; }
    void face(const viewer::ToolPlane& plane) override;

    // Where the running tool draws, for panes made while it runs.
    std::optional<view::DrawingPlane> plane() const {
        return tool_ ? std::optional<view::DrawingPlane>(drawingPlane(plane_)) : std::nullopt;
    }
    void attach(ViewerPanel* panel) { panels_.push_back(panel); }
    void detach(ViewerPanel* panel) { std::erase(panels_, panel); }

private:
    void applyAll();

    viewer::IViewTool* tool_ = nullptr;
    viewer::ToolPlane plane_{};
    std::vector<ViewerPanel*> panels_;
};

// The panel: a small toolbar over 1, 2 or 4 panes. Each pane is a ViewWidget
// with its own camera; the toolbar's views turn the active pane (the one last
// clicked). Without 3D graphics, a FlatView takes the panes' place.
class ViewerPanel final : public QWidget {
public:
    // `number` counts the viewers opened (1 for the first), so each pane has a
    // key the view options can remember, like "viewer1.pane2".
    ViewerPanel(plugin::PluginContext& ctx, ViewerService& service, int number)
        : ctx_(ctx), service_(service), options_(ctx.service<viewoptions::IViewOptions>()), number_(number) {
        service_.attach(this);
        setObjectName(QStringLiteral("kh.viewer.panel"));
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        toolbar_ = new QToolBar(this);
        toolbar_->setObjectName(QStringLiteral("kh.viewer.toolbar"));
        toolbar_->setIconSize(QSize(16, 16));
        toolbar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
        layout->addWidget(toolbar_);
        buildToolbar();

        body_ = new QWidget(this);
        auto* bodyLayout = new QVBoxLayout(body_);
        bodyLayout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(body_, 1);

        using namespace plugin;
        auto later = [this](const auto&) { scheduleRefresh(); };
        subs_.push_back(ctx_.events().subscribe<ActiveDocumentChanged>(later));
        subs_.push_back(ctx_.events().subscribe<DocumentClosed>(later));
        subs_.push_back(ctx_.events().subscribe<RebuildFinished>(later));
        subs_.push_back(ctx_.events().subscribe<SelectionChanged>(later));
        subs_.push_back(
            ctx_.events().subscribe<OverlaysChanged>([this](const OverlaysChanged&) { showOverlays(); }));
        subs_.push_back(ctx_.events().subscribe<viewoptions::ViewOptionChanged>(
            [this](const viewoptions::ViewOptionChanged&) { showOwnBars(); }));

        problem_ = view::graphicsProblem();
        if (problem_.isEmpty()) {
            setPaneLayout(view::PaneLayout::Single);
        } else {
            showFlat();
        }
        refresh();
    }

    ~ViewerPanel() override {
        service_.detach(this);
        // The window is closing: forget the panes, but keep their own options
        // for next time.
        if (options_) {
            for (std::size_t i = 0; i < panes_.size(); ++i) options_->removePane(paneKey(i), false);
        }
    }

    // The running tool's plane (or none) in every pane.
    void applyTool() {
        const auto plane = service_.plane();
        for (auto* pane : panes_) pane->setDrawingPlane(plane);
        if (flat_) flat_->setDrawingPlane(plane);
    }

    // Turns the active pane to look straight at a plane.
    void face(const viewer::ToolPlane& plane) {
        if (auto* pane = activePane()) pane->lookAlong({drawingPlane(plane).normal, plane.yAxis});
    }

private:
    // Sends a view's drawing events to the running tool.
    template <typename View>
    void forward(View* view, std::string key) {
        auto point = [key](const view::PlanePoint& p) { return viewer::ToolPoint{p.at, p.pixel, key}; };
        connect(view, &View::planeMoved, this, [this, point](const view::PlanePoint& p) {
            if (auto* t = service_.tool()) t->moved(point(p));
        });
        connect(view, &View::planeClicked, this, [this, point](const view::PlanePoint& p) {
            if (auto* t = service_.tool()) t->clicked(point(p));
        });
        connect(view, &View::planeCancelled, this, [this] {
            if (auto* t = service_.tool()) t->cancelled();
        });
    }

    std::string paneKey(std::size_t i) const {
        return "viewer" + std::to_string(number_) + ".pane" + std::to_string(i + 1);
    }

    // "Viewer 1", or "Viewer 1, top left pane" when there's more than one.
    std::string paneName(std::size_t i) const {
        static const char* const two[2][2] = {{"left", "right"}, {"top", "bottom"}};
        static const char* const four[] = {"top left", "top right", "bottom left", "bottom right"};
        const std::string viewer = "Viewer " + std::to_string(number_);
        switch (layout_) {
            case view::PaneLayout::Single:
                return viewer;
            case view::PaneLayout::SideBySide:
                return viewer + ", " + two[0][i % 2] + " pane";
            case view::PaneLayout::Stacked:
                return viewer + ", " + two[1][i % 2] + " pane";
            case view::PaneLayout::Four:
                return viewer + ", " + four[i % 4] + " pane";
        }
        return viewer;
    }

    void buildToolbar() {
        // Panes: one button that opens a menu of layouts.
        auto* panes = new QToolButton(toolbar_);
        panes->setObjectName(QStringLiteral("kh.viewer.panes"));
        panes->setText(tr("Panes"));
        panes->setToolTip(tr("Split the viewer into 1, 2 or 4 panes, each with its own camera."));
        panes->setPopupMode(QToolButton::InstantPopup);
        auto* menu = new QMenu(panes);
        auto* group = new QActionGroup(menu);
        for (auto layout : view::allLayouts()) {
            auto* a = menu->addAction(qs(view::layoutName(layout)));
            a->setObjectName(QStringLiteral("kh.viewer.layout.") + slug(view::layoutName(layout)));
            a->setCheckable(true);
            a->setChecked(layout == view::PaneLayout::Single);
            group->addAction(a);
            connect(a, &QAction::triggered, this, [this, layout] { setPaneLayout(layout); });
        }
        panes->setMenu(menu);
        toolbar_->addWidget(panes);
        toolbar_->addSeparator();

        // The usual views get a button each; the rest are in a menu.
        auto add = [this](view::StandardView v, QWidget* into) {
            auto* a = new QAction(qs(view::viewName(v)), into);
            a->setObjectName(QStringLiteral("kh.viewer.view.") + slug(view::viewName(v)));
            a->setToolTip(tr("Look at the design from the %1, in the active pane.")
                              .arg(qs(view::viewName(v)).toLower()));
            connect(a, &QAction::triggered, this, [this, v] {
                if (auto* pane = activePane()) pane->setStandardView(v);
            });
            into->addAction(a);
            return a;
        };
        for (auto v : {view::StandardView::Front, view::StandardView::Top, view::StandardView::Right,
                       view::StandardView::Iso}) {
            add(v, toolbar_);
        }
        auto* more = new QToolButton(toolbar_);
        more->setText(tr("More views"));
        more->setPopupMode(QToolButton::InstantPopup);
        auto* moreMenu = new QMenu(more);
        for (auto v : {view::StandardView::Back, view::StandardView::Left, view::StandardView::Bottom}) {
            add(v, moreMenu);
        }
        more->setMenu(moreMenu);
        toolbar_->addWidget(more);
        toolbar_->addSeparator();

        auto* fit = toolbar_->addAction(tr("Fit all"));
        fit->setObjectName(QStringLiteral("kh.viewer.fit"));
        fit->setToolTip(tr("Zoom so every body fits, in every pane."));
        connect(fit, &QAction::triggered, this, [this] {
            for (auto* pane : panes_) pane->fitAll();
        });
    }

    // Splits the viewer. Panes are kept when they can be, and every pane turns
    // to the layout's starting view.
    void setPaneLayout(view::PaneLayout layout) {
        if (!problem_.isEmpty()) return;
        layout_ = layout;
        const int count = view::paneCount(layout);
        while (static_cast<int>(panes_.size()) > count) {
            // Closed for good: the pane's own options go with it.
            if (options_) options_->removePane(paneKey(panes_.size() - 1), true);
            bars_.pop_back();
            delete frames_.back();  // deletes its pane too
            frames_.pop_back();
            panes_.pop_back();
        }
        while (static_cast<int>(panes_.size()) < count) addPane();
        active_ = std::min(active_, count - 1);

        // Put the frames into fresh splitters, then throw the old ones away.
        auto* old = splitter_;
        auto splitter = [](Qt::Orientation o) {
            auto* s = new QSplitter(o);
            s->setChildrenCollapsible(false);
            s->setHandleWidth(4);
            return s;
        };
        switch (layout) {
            case view::PaneLayout::Single:
            case view::PaneLayout::SideBySide:
                splitter_ = splitter(Qt::Horizontal);
                for (auto* f : frames_) splitter_->addWidget(f);
                break;
            case view::PaneLayout::Stacked:
                splitter_ = splitter(Qt::Vertical);
                for (auto* f : frames_) splitter_->addWidget(f);
                break;
            case view::PaneLayout::Four: {
                splitter_ = splitter(Qt::Vertical);
                for (int row = 0; row < 2; ++row) {
                    auto* r = splitter(Qt::Horizontal);
                    r->addWidget(frames_[static_cast<std::size_t>(row * 2)]);
                    r->addWidget(frames_[static_cast<std::size_t>(row * 2 + 1)]);
                    splitter_->addWidget(r);
                }
                break;
            }
        }
        body_->layout()->addWidget(splitter_);
        delete old;
        const auto views = view::defaultViews(layout);
        for (std::size_t i = 0; i < panes_.size(); ++i) panes_[i]->setStandardView(views[i]);
        if (auto* a =
                findChild<QAction*>(QStringLiteral("kh.viewer.layout.") + slug(view::layoutName(layout))))
            a->setChecked(true);
        // Tell the view options about the panes (their names depend on the layout).
        if (options_) {
            for (std::size_t i = 0; i < panes_.size(); ++i) options_->addPane({paneKey(i), paneName(i)});
        }
        showActive();
        showOverlays();
        showOwnBars();
    }

    void addPane() {
        auto* frame = new QFrame;
        frame->setObjectName(QStringLiteral("kh.viewer.paneframe"));
        auto* l = new QVBoxLayout(frame);
        l->setContentsMargins(2, 2, 2, 2);
        l->setSpacing(0);
        const std::size_t index = panes_.size();

        // Shown when the pane has its own view options.
        auto* bar = new QWidget(frame);
        bar->setObjectName(QStringLiteral("kh.viewer.ownbar"));
        bar->setStyleSheet(QStringLiteral("QWidget#kh\\.viewer\\.ownbar { background: #fff4d6; }"));
        auto* barLayout = new QHBoxLayout(bar);
        barLayout->setContentsMargins(6, 2, 2, 2);
        auto* note = new QLabel(tr("Own view options"), bar);
        note->setObjectName(QStringLiteral("kh.viewer.ownnote"));
        barLayout->addWidget(note, 1);
        auto* same = new QToolButton(bar);
        same->setObjectName(QStringLiteral("kh.viewer.ownreset"));
        same->setText(tr("Same as all panes"));
        same->setToolTip(tr("Use the same view options as all the other panes."));
        connect(same, &QToolButton::clicked, this, [this, index] {
            if (options_) options_->resetPane(paneKey(index));
        });
        barLayout->addWidget(same);
        bar->hide();
        l->addWidget(bar);
        bars_.push_back(bar);

        auto* pane = new view::ViewWidget(frame);
        pane->setObjectName(QStringLiteral("kh.viewer.pane"));
        l->addWidget(pane);
        pane->setContent(content_);
        pane->setOverlays(viewOverlays(ctx_.overlays().forPane(paneKey(index))));
        connect(pane, &view::ViewWidget::picked, this, [this](const std::vector<FeatureId>& ids) {
            const DocId doc = ctx_.documents().active();
            if (ctx_.documents().find(doc)) ctx_.selection().set(doc, ids);
        });
        connect(pane, &view::ViewWidget::activated, this, [this, pane] {
            const auto it = std::find(panes_.begin(), panes_.end(), pane);
            if (it == panes_.end()) return;
            active_ = static_cast<int>(it - panes_.begin());
            if (options_) options_->setActivePane(paneKey(static_cast<std::size_t>(active_)));
            showActive();
        });
        connect(pane, &view::ViewWidget::menuRequested, this, [this, index](QPoint at) {
            if (options_) options_->showPaneMenu(paneKey(index), at.x(), at.y());
        });
        connect(pane, &view::ViewWidget::failed, this, [this](const QString& why) {
            problem_ = why;
            QMetaObject::invokeMethod(this, [this] { showFlat(); }, Qt::QueuedConnection);
        });
        forward(pane, paneKey(index));
        pane->setDrawingPlane(service_.plane());
        frames_.push_back(frame);
        panes_.push_back(pane);
    }

    // A thin blue frame marks the active pane, when there is more than one.
    void showActive() {
        for (std::size_t i = 0; i < frames_.size(); ++i) {
            const bool on = panes_.size() > 1 && static_cast<int>(i) == active_;
            frames_[i]->setStyleSheet(
                on ? QStringLiteral("QFrame#kh\\.viewer\\.paneframe { border: 2px solid #3d8bfd; }")
                   : QStringLiteral("QFrame#kh\\.viewer\\.paneframe { border: 2px solid transparent; }"));
        }
    }

    view::ViewWidget* activePane() const {
        if (panes_.empty()) return nullptr;
        return panes_[static_cast<std::size_t>(std::clamp(active_, 0, static_cast<int>(panes_.size()) - 1))];
    }

    // 3D graphics can't work here: the flat view, and a card saying why.
    void showFlat() {
        if (flat_) return;
        for (auto* f : frames_) f->hide();
        delete splitter_;
        splitter_ = nullptr;
        if (options_) {
            for (std::size_t i = 0; i < panes_.size(); ++i) options_->removePane(paneKey(i), false);
        }
        frames_.clear();
        panes_.clear();
        bars_.clear();
        toolbar_->hide();
        flat_ = new view::FlatView(body_);
        body_->layout()->addWidget(flat_);
        flat_->setOverlays(viewOverlays(ctx_.overlays().forPane({})));
        forward(flat_, paneKey(0));
        flat_->setDrawingPlane(service_.plane());
        refresh();
    }

    // Plugins' overlays (the grid, ...): the viewer draws them without knowing
    // what they are. A pane draws its own overlays instead of the every-pane
    // ones with the same name.
    void showOverlays() {
        for (std::size_t i = 0; i < panes_.size(); ++i)
            panes_[i]->setOverlays(viewOverlays(ctx_.overlays().forPane(paneKey(i))));
        if (flat_) flat_->setOverlays(viewOverlays(ctx_.overlays().forPane({})));
    }

    // The note above a pane that has its own view options, naming them.
    void showOwnBars() {
        for (std::size_t i = 0; i < bars_.size(); ++i) {
            const auto own = options_ ? options_->ownOptions(paneKey(i)) : std::vector<std::string>{};
            QStringList names;
            for (const auto& id : own) {
                if (const auto* o = options_->find(id)) names << qs(o->text);
            }
            bars_[i]->setVisible(!own.empty());
            bars_[i]->setToolTip(tr("Different in this pane: %1. Right-click in the pane to change them.")
                                     .arg(names.join(QStringLiteral(", "))));
        }
    }

    void scheduleRefresh() {
        if (refreshPending_) return;
        refreshPending_ = true;
        QMetaObject::invokeMethod(this, [this] { refresh(); }, Qt::QueuedConnection);
    }

    void refresh() {
        refreshPending_ = false;
        content_ = viewerScene(ctx_.documents(), ctx_.selection());
        for (auto* pane : panes_) pane->setContent(content_);
        if (flat_) flat_->setText(flatText());
    }

    QString flatText() const {
        QString text = problem_;
        const DocId id = ctx_.documents().active();
        const auto* doc = ctx_.documents().find(id);
        const auto* report = ctx_.documents().lastRebuild(id);
        if (!doc || doc->featureCount() == 0) {
            text += tr("\n\nStart with Create > Box or Create > Cylinder.");
        } else if (report) {
            QStringList names;
            for (const auto& body : report->bodies()) {
                if (const auto* f = doc->find(body)) names << QString::fromStdString(f->name());
            }
            text += tr("\n\nFinished bodies: %1")
                        .arg(names.isEmpty() ? tr("none yet") : names.join(QStringLiteral(", ")));
        }
        return text;
    }

    plugin::PluginContext& ctx_;
    ViewerService& service_;
    viewoptions::IViewOptions* options_ = nullptr;  // null if kh.viewoptions isn't loaded
    int number_ = 1;
    std::vector<QWidget*> bars_;  // one per pane: "This pane has its own view options"
    QToolBar* toolbar_ = nullptr;
    QWidget* body_ = nullptr;
    QSplitter* splitter_ = nullptr;
    view::FlatView* flat_ = nullptr;
    std::vector<QFrame*> frames_;
    std::vector<view::ViewWidget*> panes_;
    view::PaneLayout layout_ = view::PaneLayout::Single;
    int active_ = 0;
    view::SceneContent content_;
    QString problem_;
    bool refreshPending_ = false;
    std::vector<plugin::Subscription> subs_;
};

void ViewerService::startTool(viewer::IViewTool& tool, const viewer::ToolPlane& plane) {
    tool_ = &tool;
    plane_ = plane;
    applyAll();
}

void ViewerService::stopTool(const viewer::IViewTool& tool) {
    if (tool_ != &tool) return;
    tool_ = nullptr;
    applyAll();
}

void ViewerService::face(const viewer::ToolPlane& plane) {
    for (auto* panel : panels_) panel->face(plane);
}

void ViewerService::applyAll() {
    for (auto* panel : panels_) panel->applyTool();
}

void registerViewer(plugin::PluginContext& ctx) {
    // The service lives as long as the plugin: a listener Khan keeps while the
    // plugin is loaded holds it (and so does the panel factory below).
    auto service = std::make_shared<ViewerService>();
    ctx.provide<viewer::IViewer>(*service);
    ctx.listen<plugin::DocumentClosed>([service](const plugin::DocumentClosed&) {});
    auto* ui = ctx.service<ui::UiRegistry>();
    if (!ui) {
        ctx.log("No desktop app here, so the 3D viewer isn't shown.");
        return;
    }
    auto opened = std::make_shared<int>(0);  // viewers opened so far, to number them
    ui::PanelSpec spec{"kh.viewer.main", QObject::tr("Viewer"), ui::PanelArea::Center,
                       [&ctx, opened, service] { return new ViewerPanel(ctx, *service, ++*opened); }};
    spec.allowMultiple = true;
    ui->addPanel(std::move(spec));
}

}  // namespace

view::SceneContent viewerScene(const plugin::DocumentList& docs, const plugin::Selection& selection) {
    view::SceneContent out;
    const DocId id = docs.active();
    const auto* report = docs.lastRebuild(id);
    const auto* design = docs.find(id);
    if (!report || !design) return out;
    if (selection.doc() == id) out.selected = selection.features();
    // Each body is drawn in its material's color.
    auto colorOf = [design](const FeatureId& f) {
        const auto c = design->materialOf(f).color;
        return view::Rgb{c.r, c.g, c.b};
    };
    const auto& bodies = report->bodies();
    for (const auto& body : bodies) {
        if (const auto* shape = report->shapeOf(body))
            out.bodies.push_back({body, *shape, false, colorOf(body)});
    }
    // A selected step that a later step used up (the tool of a cut, say) is
    // drawn see-through, so the student can see what they picked.
    for (const auto& picked : out.selected) {
        if (std::find(bodies.begin(), bodies.end(), picked) != bodies.end()) continue;
        const auto* r = report->result(picked);
        if (r && r->status == model::FeatureStatus::Ok && r->shape.valid())
            out.bodies.push_back({picked, r->shape, true, colorOf(picked)});
    }
    return out;
}

std::vector<view::ViewOverlay> viewOverlays(const std::vector<plugin::Overlay>& overlays) {
    std::vector<view::ViewOverlay> out;
    for (const auto& o : overlays) {
        if (!o.visible) continue;
        view::ViewOverlay v;
        switch (o.shape) {
            case plugin::OverlayShape::Lines:
                v.shape = view::OverlayShape::Lines;
                break;
            case plugin::OverlayShape::Points:
                v.shape = view::OverlayShape::Points;
                break;
            case plugin::OverlayShape::Lattice:
                v.shape = view::OverlayShape::Lattice;
                break;
            case plugin::OverlayShape::Labels:
                v.shape = view::OverlayShape::Labels;
                break;
        }
        v.points = o.points;
        v.texts = o.texts;
        v.origin = o.lattice.origin;
        v.xAxis = o.lattice.xAxis;
        v.yAxis = o.lattice.yAxis;
        v.spacing = o.lattice.spacing;
        v.halfSize = o.lattice.halfSize;
        v.dots = o.lattice.dots;
        v.color = {o.color.r, o.color.g, o.color.b};
        v.alpha = o.color.a;
        v.size = o.size;
        out.push_back(std::move(v));
    }
    return out;
}

plugin::BuiltinPlugin viewerPlugin() {
    return {kManifest, &registerViewer};
}

}  // namespace kh::plugins
