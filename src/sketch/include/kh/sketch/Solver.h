#pragma once

/// @file
/// The sketch solver interface: moves a sketch's geometry so its constraints are true.

#include <vector>

#include "kh/core/Diagnostic.h"
#include "kh/core/Id.h"
#include "kh/sketch/Sketch.h"

namespace kh::sketch {

/// How a solve ended.
enum class SolveStatus {
    Solved,       ///< Every constraint is true.
    Conflicting,  ///< Some constraints can't all be true at once; see SolveResult::conflicting.
    Failed,       ///< The solver couldn't find positions that make the constraints true.
    Invalid,      ///< The sketch has a problem check() finds; nothing was solved.
};

/// What a solve gives back.
struct SolveResult {
    SolveStatus status = SolveStatus::Failed;  ///< How it ended.
    /// The entities, in the sketch's order, where the solver put them. When the
    /// solve didn't succeed, they're where they were.
    std::vector<Entity> entities{};
    /// How many ways the geometry can still move (degrees of freedom): 0 means
    /// fully constrained. -1 when it couldn't be worked out.
    int freedom = -1;
    std::vector<ConstraintId> conflicting{};  ///< Constraints that fight each other (with Conflicting).
    /// Constraints that say something the others already say. The sketch still
    /// solves, but deleting one of them is cleaner.
    std::vector<ConstraintId> redundant{};
    Diagnostics issues{};  ///< What went wrong or is worth knowing, in plain words.

    /// Checks whether the solve succeeded.
    /// @return True if status is Solved.
    bool ok() const { return status == SolveStatus::Solved; }
};

/// Solves sketches. Khan's sketch code only talks to the solver through this
/// interface, so the solver can be tested and replaced.
///
/// Rules for implementations:
///  - Never throw; report problems in SolveResult::issues.
///  - Be deterministic: the same sketch gives the same result.
///  - Keep every entity's id, kind and order; change only positions and sizes.
///
/// @code
/// auto solver = kh::sketch::makePlaneGcsSolver();
/// const auto result = solver->solve(sketch);
/// if (result.ok()) sketch.entities = result.entities;
/// @endcode
class ISketchSolver {
public:
    /// Destroys the solver.
    virtual ~ISketchSolver() = default;

    /// Moves the geometry so every constraint is true, as little as it can.
    /// @param sketch The sketch, with its geometry where it is now (the starting guess).
    /// @return The solved geometry and what the solver found.
    virtual SolveResult solve(const Sketch& sketch) = 0;
};

}  // namespace kh::sketch
