#pragma once

/// @file
/// Sketch dimensions whose numbers come from formulas, like `Length1 = wall * 2`.

#include <string>
#include <string_view>

#include "kh/model/Expression.h"
#include "kh/model/Parameters.h"
#include "kh/sketch/Sketch.h"

namespace kh::model {

/// Gives what a dimension measures: an angle for Angle, a length for the
/// others (Length, Distance, horizontal and vertical distance, Radius, Diameter).
/// @param kind A dimension's kind (see sketch::isDimension()).
/// @return kAngle or kLength.
Dimension dimensionOf(sketch::ConstraintKind kind);

/// Works out a formula for a dimension, and checks it gives the right kind of
/// value (a length for a Length, an angle for an Angle), and more than 0 mm for
/// a Length, Distance, Radius or Diameter.
/// @param kind The dimension's kind.
/// @param formula The formula, like "wall * 2".
/// @param values The design's parameters, worked out.
/// @param what How messages name the dimension, like "Length1 in Sketch1".
/// @return The value (mm, or radians for an angle), or why not, in plain words.
EvalResult evalDimension(sketch::ConstraintKind kind, std::string_view formula, const ParameterValues& values,
                         std::string_view what);

/// Checks whether a sketch has a dimension whose formula uses a name.
/// @param sketch The sketch.
/// @param name A parameter's name.
/// @return The names of those dimensions, in the sketch's order.
std::vector<std::string> dimensionsUsing(const sketch::Sketch& sketch, std::string_view name);

}  // namespace kh::model
