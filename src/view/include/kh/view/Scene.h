#pragma once

/// @file
/// What a 3D view shows: bodies, the selection and the grid. Plain data, no Qt or OCCT.

#include <vector>

#include "kh/core/Id.h"
#include "kh/kernel/Types.h"

namespace kh::view {

/// One shape to draw, belonging to one step of the design.
struct SceneBody {
    FeatureId feature{};          ///< The step the shape belongs to; picking it selects this step.
    kernel::ShapeHandle shape{};  ///< The shape, from the OCCT kernel.
    bool ghost = false;           ///< Drawn see-through: a step that is used up by a later step.
};

/// Everything a view draws of a design.
struct SceneContent {
    std::vector<SceneBody> bodies{};    ///< The shapes, in design order.
    std::vector<FeatureId> selected{};  ///< Selected steps, in picking order; drawn highlighted.
};

/// The grid on the floor (the XY plane), as a view draws it.
struct ViewGrid {
    bool visible = true;  ///< Draw it at all.
    double spacing = 10;  ///< Distance between grid lines, in mm.
    bool lines = true;    ///< Lines, or dots when false.
};

/// How much of the grid to draw.
struct GridExtent {
    double step = 10;       ///< Distance between drawn lines or dots, in mm.
    double halfSize = 100;  ///< The grid reaches this far from the origin each way, in mm.
};

/// Works out how much grid to draw for a spacing: 50 lines each way, but at
/// least 100 mm, and never more than 100 lines, so a fine grid stays quick to draw.
/// @param spacing The grid spacing, in mm (more than 0).
/// @return The drawn step (the spacing) and how far the grid reaches.
GridExtent gridExtent(double spacing);

/// Works out the selection after a click in a view.
///
/// The view knows which steps are selected after the click, but not the order
/// they were picked in. Steps that stay selected keep their place; new ones are
/// added at the end, in the order given.
/// @param before The selection before the click, in picking order.
/// @param after The steps selected after the click, in any order.
/// @return @p after, in picking order.
std::vector<FeatureId> mergePick(const std::vector<FeatureId>& before, const std::vector<FeatureId>& after);

}  // namespace kh::view
