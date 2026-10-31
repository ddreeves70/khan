#pragma once

/// @file
/// The viewer contract: what other plugins may use from kh.viewer (drawing tools).
///
/// A plugin that draws in the viewers with the mouse (the sketcher, say) starts
/// a tool on a plane. While it runs, a left click in any pane reports the point
/// on that plane instead of orbiting or selecting, moving the mouse reports it
/// too, and a left drag reports where it started and where it is; panning and
/// zooming still work. List "kh.viewer" in "requires" or "uses" in your
/// plugin.json, then:
///
/// @code
/// struct Dots final : kh::viewer::IViewTool {
///     void moved(const kh::viewer::ToolPoint&) override {}
///     void clicked(const kh::viewer::ToolPoint& p) override { /* p.at is the point, in mm */ }
///     void cancelled() override {}  // Escape
/// };
/// auto* viewer = ctx.service<kh::viewer::IViewer>();
/// viewer->startTool(dots, {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}});  // the ground (XY)
/// viewer->face({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}});            // look straight down at it
/// @endcode

#include <string>
#include <vector>

#include "kh/kernel/Types.h"

namespace kh::viewer {

/// A flat plane a tool draws on: a point on it and two directions along it.
/// The tool's x and y are measured along `xAxis` and `yAxis`; the plane is seen
/// from the side `xAxis` × `yAxis` points to.
struct ToolPlane {
    kernel::Vec3 origin{};        ///< Where the tool's x and y are 0, in mm.
    kernel::Vec3 xAxis{1, 0, 0};  ///< The tool's x direction (a unit vector).
    kernel::Vec3 yAxis{0, 1, 0};  ///< The tool's y direction (a unit vector, square to xAxis).
};

/// Where the mouse is on the tool's plane.
struct ToolPoint {
    kernel::Vec3 at{};   ///< The point, in mm, in the design's x, y and z.
    double pixel = 0;    ///< How many mm one screen pixel covers there: use it for "close enough".
    std::string pane{};  ///< The pane it's in (its view options key, e.g. "viewer1.pane1").
    bool add = false;    ///< Ctrl or Shift was held: add to what's picked instead of replacing it.
};

/// A tool that draws with the mouse. The viewer calls it while it runs.
class IViewTool {
public:
    /// Destroys the tool.
    virtual ~IViewTool() = default;

    /// The mouse moved over the plane (for previews).
    /// @param point Where.
    virtual void moved(const ToolPoint& point) = 0;

    /// The student clicked on the plane with the left button.
    /// @param point Where.
    virtual void clicked(const ToolPoint& point) = 0;

    /// The student is dragging with the left button: pressed, then moved more
    /// than a few pixels. Called on every move until dropped(). A drag is never
    /// also a click. Does nothing unless the tool overrides it.
    /// @param from Where the button went down.
    /// @param to Where the mouse is now.
    virtual void dragged(const ToolPoint& from, const ToolPoint& to) {
        (void)from;
        (void)to;
    }

    /// A drag ended: the left button came up. Does nothing unless the tool
    /// overrides it.
    /// @param from Where the button went down.
    /// @param to Where it came up.
    virtual void dropped(const ToolPoint& from, const ToolPoint& to) {
        (void)from;
        (void)to;
    }

    /// The student pressed Escape in a view (also while dragging: then forget the
    /// drag, and ignore the dropped() that follows).
    virtual void cancelled() = 0;
};

/// What a handle looks like, and how it drags.
enum class HandleKind {
    Arrow,   ///< An arrow: drags along its direction only.
    Square,  ///< A small square: drags anywhere on its flat plane.
};

/// Something drawn in every viewer for the student to drag, like the arrows on
/// a picked body (see IViewer::setHandles()). Handles are drawn over the design,
/// so they can always be grabbed, and aren't shown while a tool runs.
struct Handle {
    std::string id{};                     ///< Says which handle was dragged, like "x".
    HandleKind kind = HandleKind::Arrow;  ///< An arrow or a square.
    kernel::Vec3 at{};                    ///< Arrow: where it starts. Square: its middle. In mm.
    /// Arrow: which way it points and drags (a unit vector). Square: the direction
    /// square to its plane (a unit vector); it drags on that plane.
    kernel::Vec3 direction{1, 0, 0};
    /// Arrow: how long. Square: how long each side is. In screen pixels: a handle
    /// stays the same size on the screen however far the view zooms.
    double length = 80;
    int r = 0;  ///< Its color: red, 0 to 255.
    int g = 0;  ///< Green, 0 to 255.
    int b = 0;  ///< Blue, 0 to 255.
};

/// Whoever put handles in the viewers. The viewer calls it while one is dragged.
class IHandleUser {
public:
    /// Destroys it.
    virtual ~IHandleUser() = default;

    /// A handle is being dragged. Called on every move, until dropped() or cancelled().
    /// @param id The handle's id.
    /// @param by How far it has been dragged since the button went down, along x, y and z (mm).
    virtual void dragged(const std::string& id, const kernel::Vec3& by) = 0;

    /// A drag ended: the button came up.
    /// @param id The handle's id.
    /// @param by How far it was dragged, along x, y and z (mm).
    virtual void dropped(const std::string& id, const kernel::Vec3& by) = 0;

    /// The student pressed Escape while dragging: put back what the drag changed.
    virtual void cancelled() = 0;
};

/// The viewers, as other plugins see them.
class IViewer {
public:
    /// The service's name and version.
    /// Version 2 added dragged(), dropped() and ToolPoint::add. Version 3 added
    /// handles (setHandles(), clearHandles()).
    static constexpr const char* kServiceName = "kh.viewer/3";

    /// Destroys the service.
    virtual ~IViewer() = default;

    /// Starts a tool on a plane in every pane of every viewer, stopping the one
    /// that was running.
    /// @param tool The tool. It must stay alive until stopTool() or another startTool().
    /// @param plane The plane it draws on.
    virtual void startTool(IViewTool& tool, const ToolPlane& plane) = 0;

    /// Stops a tool, if it's the one running. Clicks orbit and select again.
    /// @param tool The tool.
    virtual void stopTool(const IViewTool& tool) = 0;

    /// Gives the tool that's running.
    /// @return It, or nullptr.
    virtual IViewTool* tool() const = 0;

    /// Turns each viewer's active pane to look straight at a plane, and fits.
    /// @param plane The plane.
    virtual void face(const ToolPlane& plane) = 0;

    /// Puts handles in every pane of every viewer, in place of the ones there
    /// (yours or another plugin's). Give them again as the thing they move
    /// moves; a drag carries on with the handle as it was when pressed.
    /// @param user Told when they're dragged. It must stay alive until clearHandles()
    ///        or another setHandles().
    /// @param handles The handles; empty to show none.
    virtual void setHandles(IHandleUser& user, std::vector<Handle> handles) = 0;

    /// Takes away a user's handles, if they're the ones shown.
    /// @param user The user.
    virtual void clearHandles(const IHandleUser& user) = 0;

    /// Gives the handles shown.
    /// @return The handles last given to setHandles(), or none.
    virtual std::vector<Handle> handles() const = 0;

    /// Gives who put the handles there.
    /// @return It, or nullptr if none are shown.
    virtual IHandleUser* handleUser() const = 0;
};

}  // namespace kh::viewer
