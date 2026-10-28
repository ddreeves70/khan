#pragma once

/// @file
/// The sketch solver built on PlaneGCS, FreeCAD's constraint solver.

#include <memory>

#include "kh/sketch/Solver.h"

namespace kh::sketch {

/// Makes a sketch solver that uses PlaneGCS (its DogLeg method).
///
/// PlaneGCS is LGPL-2.1-or-later and lives in its own shared library,
/// `kh_planegcs`; nothing outside this library sees its types.
/// @return The solver.
std::unique_ptr<ISketchSolver> makePlaneGcsSolver();

}  // namespace kh::sketch
