#pragma once

/// @file
/// The desktop app's service for plugins that add panels (Qt).

#include <QString>
#include <QWidget>

#include <functional>
#include <string>

#include "kh/core/Diagnostic.h"

/// The Qt side of the plugin API: what only the desktop app offers.
namespace kh::ui {

/// Where a panel docks when Khan starts with its default layout. The student can
/// move it anywhere afterwards, and Khan remembers.
enum class PanelArea {
    Left,    ///< Left of the 3D view, like the feature tree.
    Right,   ///< Right of the 3D view, like Properties.
    Bottom,  ///< Below the 3D view, like the timeline.
    Center,  ///< A tab next to the 3D view.
};

/// A dockable panel a plugin adds to the desktop app.
struct PanelSpec {
    std::string id{};  ///< Stable id; must start with the plugin's id and a dot, e.g. "kh.featuretree.panel".
    QString title{};   ///< The panel's tab title, e.g. "Feature tree".
    PanelArea area = PanelArea::Left;    ///< Where it docks by default.
    std::function<QWidget*()> create{};  ///< Makes the panel's widget. Khan owns the widget.
    /// The student can open more than one of this panel (Window > New ...), like
    /// a second viewer. Each one is made by its own call to create().
    bool allowMultiple = false;
};

/// Something a plugin shows in the desktop app's status bar, at the bottom right.
struct StatusItemSpec {
    std::string id{};                    ///< Stable id; must start with the plugin's id and a dot.
    std::function<QWidget*()> create{};  ///< Makes the widget, usually a QLabel. Khan owns it.
};

/// Lets a plugin add panels to the desktop app.
///
/// Only the desktop app provides it, so a plugin asks for it and adds its panels
/// only when it is there; in `kh_cli` the same plugin still adds its commands.
///
/// @code
/// void registerTree(kh::plugin::PluginContext& ctx) {
///     auto* ui = ctx.service<kh::ui::UiRegistry>();
///     if (!ui) return;  // not in the desktop app
///     ui->addPanel({"kh.featuretree.panel", "Feature tree", kh::ui::PanelArea::Left,
///                   [&ctx] { return new FeatureTree(ctx); }});
/// }
/// @endcode
class UiRegistry {
public:
    /// The name plugins use to find this service: PluginContext::service<UiRegistry>().
    static constexpr const char* kServiceName = "kh.ui";

    /// Destroys the registry.
    virtual ~UiRegistry() = default;

    /// Adds a dockable panel. Call it from the plugin's register function.
    /// Khan makes the panel when the main window opens, and drops it if the
    /// plugin fails to load.
    /// @param panel Its id, title, default place and how to make it.
    virtual void addPanel(PanelSpec panel) = 0;

    /// Adds something to the status bar, like "Grid 10 mm, lines". Call it from
    /// the plugin's register function. Khan makes the widget when the main window
    /// opens, puts it at the bottom right, and drops it if the plugin fails to
    /// load. Keep it up to date by listening to events.
    /// @param item Its id and how to make it.
    virtual void addStatusItem(StatusItemSpec item) = 0;

    /// Tells the student about problems, for example why an edit was refused.
    /// The desktop app shows them in the status bar; errors stay until the next
    /// change.
    /// @param problems What went wrong, in plain words.
    virtual void showProblems(const Diagnostics& problems) = 0;

    /// Shows a short note in the status bar, e.g. "Saved bracket.khan".
    /// @param message Plain words.
    virtual void showMessage(const QString& message) = 0;
};

}  // namespace kh::ui
