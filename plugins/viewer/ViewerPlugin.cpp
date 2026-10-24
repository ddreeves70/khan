#include "kh/plugins/Viewer.h"

#include <QAction>
#include <QActionGroup>
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QSplitter>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <string>
#include <vector>

#include "kh/model/Document.h"
#include "kh/plugin/Events.h"
#include "kh/plugin/Grid.h"
#include "kh/ui/UiRegistry.h"
#include "kh/view/FlatView.h"
#include "kh/view/Graphics.h"
#include "kh/view/Layout.h"
#include "kh/view/ViewWidget.h"

namespace kh::plugins {

namespace {

constexpr const char* kManifest = R"({
  "id": "kh.viewer",
  "name": "3D viewer",
  "version": "0.1.0",
  "description": "The 3D view of a design: panes, standard views, the view cube and the grid.",
  "requires": ["kh.modeling"]
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

view::ViewGrid toView(const plugin::GridSettings& g) {
    return {g.visible, g.spacing, g.style == plugin::GridStyle::Lines};
}

// The panel: a small toolbar over 1, 2 or 4 panes. Each pane is a ViewWidget
// with its own camera; the toolbar's views turn the active pane (the one last
// clicked). Without 3D graphics, a FlatView takes the panes' place.
class ViewerPanel final : public QWidget {
public:
    explicit ViewerPanel(plugin::PluginContext& ctx) : ctx_(ctx), grid_(ctx.service<plugin::Grid>()) {
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
            ctx_.events().subscribe<GridChanged>([this](const GridChanged& e) { showGrid(e.grid); }));

        problem_ = view::graphicsProblem();
        if (problem_.isEmpty()) {
            setPaneLayout(view::PaneLayout::Single);
        } else {
            showFlat();
        }
        refresh();
    }

private:
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
        showActive();
    }

    void addPane() {
        auto* frame = new QFrame;
        frame->setObjectName(QStringLiteral("kh.viewer.paneframe"));
        auto* l = new QVBoxLayout(frame);
        l->setContentsMargins(2, 2, 2, 2);
        auto* pane = new view::ViewWidget(frame);
        pane->setObjectName(QStringLiteral("kh.viewer.pane"));
        l->addWidget(pane);
        pane->setContent(content_);
        if (grid_) pane->setGrid(toView(grid_->settings()));
        connect(pane, &view::ViewWidget::picked, this, [this](const std::vector<FeatureId>& ids) {
            const DocId doc = ctx_.documents().active();
            if (ctx_.documents().find(doc)) ctx_.selection().set(doc, ids);
        });
        connect(pane, &view::ViewWidget::activated, this, [this, pane] {
            const auto it = std::find(panes_.begin(), panes_.end(), pane);
            if (it == panes_.end()) return;
            active_ = static_cast<int>(it - panes_.begin());
            showActive();
        });
        connect(pane, &view::ViewWidget::failed, this, [this](const QString& why) {
            problem_ = why;
            QMetaObject::invokeMethod(this, [this] { showFlat(); }, Qt::QueuedConnection);
        });
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

    // 3D graphics can't work here: the flat grid, and a card saying why.
    void showFlat() {
        if (flat_) return;
        for (auto* f : frames_) f->hide();
        delete splitter_;
        splitter_ = nullptr;
        frames_.clear();
        panes_.clear();
        toolbar_->hide();
        flat_ = new view::FlatView(body_);
        body_->layout()->addWidget(flat_);
        if (grid_) flat_->setGrid(toView(grid_->settings()));
        refresh();
    }

    void showGrid(const plugin::GridSettings& g) {
        for (auto* pane : panes_) pane->setGrid(toView(g));
        if (flat_) flat_->setGrid(toView(g));
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
    plugin::Grid* grid_ = nullptr;
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

void registerViewer(plugin::PluginContext& ctx) {
    auto* ui = ctx.service<ui::UiRegistry>();
    if (!ui) {
        ctx.log("No desktop app here, so the 3D viewer isn't shown.");
        return;
    }
    ui::PanelSpec spec{"kh.viewer.main", QObject::tr("Viewer"), ui::PanelArea::Center,
                       [&ctx] { return new ViewerPanel(ctx); }};
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

plugin::BuiltinPlugin viewerPlugin() {
    return {kManifest, &registerViewer};
}

}  // namespace kh::plugins
