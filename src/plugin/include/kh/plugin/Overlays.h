#pragma once

/// @file
/// View overlays: things plugins ask the viewers to draw, such as the grid.

#include <string>
#include <string_view>
#include <vector>

#include "kh/kernel/Types.h"
#include "kh/plugin/EventBus.h"

namespace kh::plugin {

/// A color for an overlay.
struct OverlayColor {
    int r = 0;    ///< Red, 0 to 255.
    int g = 0;    ///< Green, 0 to 255.
    int b = 0;    ///< Blue, 0 to 255.
    int a = 255;  ///< How solid: 0 is invisible, 255 is fully solid.

    /// Compares two colors.
    /// @param other The other color.
    /// @return True if every part is the same.
    bool operator==(const OverlayColor& other) const = default;
};

/// What an overlay draws.
enum class OverlayShape {
    Lines,    ///< Straight lines: each pair of `points` is one line.
    Points,   ///< A dot at each of `points`.
    Lattice,  ///< Evenly spaced lines (or dots where they cross) on a plane, like graph paper.
};

/// A lattice: lines every `spacing` mm on a plane, like graph paper.
struct OverlayLattice {
    kernel::Vec3 origin{};        ///< A crossing point, in mm.
    kernel::Vec3 xAxis{1, 0, 0};  ///< The direction of one set of lines (a unit vector).
    kernel::Vec3 yAxis{0, 1, 0};  ///< The direction of the other set (a unit vector).
    double spacing = 10;          ///< Distance between lines, in mm.
    double halfSize = 100;        ///< How far it reaches from `origin` each way, in mm.
    bool dots = false;            ///< Dots where the lines cross, instead of the lines.
};

/// Something for every viewer to draw over the design, as plain data. Viewers
/// draw overlays without knowing what they mean, so a plugin can show a grid,
/// guide lines or measurement marks without changing the viewer.
///
/// Overlays are never picked with the mouse and don't count when zooming to fit.
struct Overlay {
    OverlayShape shape = OverlayShape::Lines;  ///< What it draws.
    std::vector<kernel::Vec3> points{};        ///< For Lines and Points, in mm.
    OverlayLattice lattice{};                  ///< For Lattice.
    OverlayColor color{};                      ///< Its color.
    double size = 1;                           ///< Line width or dot size, in screen pixels.
    bool visible = true;                       ///< False to hide it without removing it.
    std::string id{};                          ///< Set by Khan: the plugin id and the overlay's name.
};

/// Sent when an overlay is added, changed or removed.
struct OverlaysChanged {
    static constexpr std::string_view kType = "kh.OverlaysChanged";  ///< Stable event type name.
    std::string id;                                                  ///< The overlay's full id.
};

/// Every plugin's overlays, in the order they were first added. The plugin host
/// keeps one; plugins reach it through PluginContext::overlays().
class OverlayList {
public:
    /// Makes an empty list.
    /// @param bus Where OverlaysChanged is sent. Must outlive the list.
    explicit OverlayList(EventBus& bus) : bus_(bus) {}

    OverlayList(const OverlayList&) = delete;
    OverlayList& operator=(const OverlayList&) = delete;

    /// Adds an overlay, or replaces the one with the same id. Sends
    /// OverlaysChanged if anything changed.
    /// @param overlay The overlay, with its full `id` set.
    void set(Overlay overlay);

    /// Removes an overlay. Sends OverlaysChanged if it was there.
    /// @param id Its full id.
    void remove(std::string_view id);

    /// Removes every overlay a plugin added, e.g. when it fails to start.
    /// @param plugin The plugin's id.
    void removeOwner(std::string_view plugin);

    /// Finds an overlay.
    /// @param id Its full id, e.g. "kh.grid.grid".
    /// @return It, or nullptr.
    const Overlay* find(std::string_view id) const;

    /// Gives every overlay, in the order they were first added.
    /// @return The overlays.
    const std::vector<Overlay>& all() const { return overlays_; }

private:
    EventBus& bus_;
    std::vector<Overlay> overlays_;
};

/// One plugin's way into the overlays. A plugin gets its own from
/// PluginContext::overlays(); the names it picks can't clash with other plugins'.
///
/// @code
/// kh::plugin::Overlay line;
/// line.points = {{0, 0, 0}, {50, 0, 0}};  // a 50 mm line along x
/// line.color = {230, 120, 20};
/// ctx.overlays().set("guide", line);    // its id is "<your plugin id>.guide"
/// @endcode
class PluginOverlays {
public:
    /// Makes a plugin's view of the list.
    /// @param list Every overlay. Must outlive this.
    /// @param plugin The plugin's id.
    PluginOverlays(OverlayList& list, std::string plugin) : list_(list), plugin_(std::move(plugin)) {}

    /// Adds or replaces one of this plugin's overlays.
    /// @param name A short name, e.g. "grid"; its full id is "<plugin id>.<name>".
    /// @param overlay What to draw.
    void set(std::string_view name, Overlay overlay);

    /// Removes one of this plugin's overlays.
    /// @param name Its short name.
    void remove(std::string_view name);

    /// Gives every plugin's overlays, for a viewer to draw.
    /// @return The overlays, in the order they were first added.
    const std::vector<Overlay>& all() const { return list_.all(); }

    /// Gives an overlay's full id.
    /// @param name Its short name.
    /// @return "<plugin id>.<name>".
    std::string fullId(std::string_view name) const { return plugin_ + "." + std::string(name); }

private:
    OverlayList& list_;
    std::string plugin_;
};

}  // namespace kh::plugin
