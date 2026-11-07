#pragma once

/// @file
/// What a viewer picks: bodies or edges, and a picked edge.

#include "sb/core/Id.h"
#include "sb/model/Naming.h"

namespace sb::plugin {

/// What clicking a body in a viewer picks.
enum class PickMode {
    Bodies,  ///< The step whose body it is (the usual).
    Edges,   ///< The edge under the mouse, for a fillet or chamfer.
};

/// One edge (or face) picked in a viewer: which body, and a lasting reference to it.
struct PickedElement {
    FeatureId body{};          ///< The step whose body it's on.
    model::GeometryRef ref{};  ///< The edge, by name (see model::ShapeNames).

    /// Checks whether two picks are the same element.
    /// @param other The other pick.
    /// @return True for the same body and reference.
    bool operator==(const PickedElement& other) const { return body == other.body && ref == other.ref; }
};

}  // namespace sb::plugin
