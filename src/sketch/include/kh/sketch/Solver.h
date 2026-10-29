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
    /// The points that can still move: some of their x and y isn't set by the
    /// constraints. Points not listed are fixed. Only filled by solve(), when it
    /// succeeds, in the sketch's order (Self, Start, End, Center for each entity).
    std::vector<PointRef> freePoints{};
    /// The entities that can still move or change size in some way (a point, a
    /// radius, an angle). Entities not listed are fully constrained. Only filled by
    /// solve(), when it succeeds, in the sketch's order.
    std::vector<EntityId> freeEntities{};
    Diagnostics issues{};  ///< What went wrong or is worth knowing, in plain words.

    /// Checks whether the solve succeeded.
    /// @return True if status is Solved.
    bool ok() const { return status == SolveStatus::Solved; }
};

/// A pull on a sketch while it's dragged with the mouse (see ISketchSolver::drag()).
struct Pull {
    /// The point pulled. With `edge`, only its entity counts.
    PointRef point{};
    Point2 to{};  ///< Where it's pulled to, in mm.
    /// Pull the edge of a circle or arc through `to` instead, so it grows or shrinks
    /// (its center stays put if the constraints let it).
    bool edge = false;
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

    /// Like solve(), while the student drags part of the sketch: every constraint
    /// stays true, and the pulled points come as close to where they're pulled as
    /// the constraints let them. Everything else moves as little as it can.
    ///
    /// Fast enough to call on every mouse move: it doesn't work out degrees of
    /// freedom, or conflicting and repeated constraints (freedom is -1, and those
    /// lists are empty). Call solve() for those when the drag ends.
    /// @code
    /// // Drag Line1's end to (30, 10) mm.
    /// const auto r = solver->drag(sketch, {{{line1, kh::sketch::PointRole::End}, {30, 10}}});
    /// @endcode
    /// @param sketch The sketch, with its geometry where it is now.
    /// @param pulls Where the mouse pulls; a pull on something that isn't in the
    ///        sketch (or an edge pull on something that isn't round) is ignored.
    /// @return The moved geometry; status Solved if every constraint is still true.
    virtual SolveResult drag(const Sketch& sketch, const std::vector<Pull>& pulls) = 0;
};

}  // namespace kh::sketch
