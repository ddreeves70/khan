#pragma once

/// @file
/// Topological naming: names for every face, edge and vertex of a step's
/// result that stay the same when the design changes, and references to them.

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "sb/kernel/Types.h"

namespace sb::model {

/// A name for every face, edge and vertex of one step's result, built from the
/// design's history, and what each looks like.
///
/// A face is named for where it came from, like "side made from Line3 by
/// Extrude1", so the name stays the same when sizes change. An edge is named for
/// the faces it borders, and a vertex for the faces around it. Two elements that
/// would get the same name get "#1", "#2", ..., in order of where their middles
/// are (along x, then y, then z), never by the kernel's order. See
/// docs/design/naming.md.
///
/// Names hold the first 8 characters of step and sketch-entity ids. They are
/// meant for matching, not for showing to people.
struct ShapeNames {
    kernel::Elements elements;       ///< The result's faces, edges and vertices (from IKernel::elements()).
    std::vector<std::string> faces;  ///< One name per face, in elements order.
    std::vector<std::string> edges;  ///< One name per edge, in elements order.
    std::vector<std::string> vertices;  ///< One name per vertex, in elements order.

    /// Gives one list of names by kind.
    /// @param kind Faces, edges or vertices.
    /// @return That list.
    const std::vector<std::string>& of(kernel::TopoKind kind) const {
        return kind == kernel::TopoKind::Face ? faces : kind == kernel::TopoKind::Edge ? edges : vertices;
    }

    /// Finds an element by its exact name.
    /// @param kind Face, edge or vertex.
    /// @param name The name.
    /// @return Its number in elements, or -1 if no element has that name.
    int find(kernel::TopoKind kind, std::string_view name) const;
};

/// A lasting reference to one face, edge or vertex of a body, as a step that
/// uses it (like a fillet) saves it.
///
/// It holds the element's name, and what it looked like when it was picked, so
/// it can still be found if its name is gone: see resolve().
struct GeometryRef {
    kernel::TopoKind kind = kernel::TopoKind::Edge;  ///< Face, edge or vertex.
    std::string name;                                ///< Its name (ShapeNames) when it was picked.
    kernel::ElementInfo signature;                   ///< What it looked like when it was picked.

    /// Checks whether two references are the same.
    /// @param other The other reference.
    /// @return True if kind and name match (the signature is only a hint).
    bool operator==(const GeometryRef& other) const { return kind == other.kind && name == other.name; }
};

/// How well a reference was found again after a change.
enum class ResolveStatus {
    Exact,    ///< An element has exactly its name.
    Matched,  ///< Its name is gone, but one element came from the same place and clearly looks most like it.
    Ambiguous,  ///< Two or more elements look about as much like it: the user must pick again.
    Missing,    ///< Nothing like it is left: the user must pick again.
};

/// What resolve() found.
struct Resolution {
    ResolveStatus status = ResolveStatus::Missing;  ///< How well it was found.
    int index = -1;               ///< The element's number in elements, for Exact and Matched; otherwise -1.
    std::vector<int> candidates;  ///< For Ambiguous: the elements it could be, best first.

    /// Checks whether the element was found.
    /// @return True for Exact and Matched.
    bool found() const { return status == ResolveStatus::Exact || status == ResolveStatus::Matched; }
};

/// Makes a reference to an element of a result.
/// @param names The result's names.
/// @param kind Face, edge or vertex.
/// @param index Its number in elements, from 0.
/// @return The reference; empty name if @p index isn't in the list.
GeometryRef referTo(const ShapeNames& names, kernel::TopoKind kind, int index);

/// Finds a referenced element in a result, after the design may have changed.
///
/// First by exact name. If the name is gone (an edit split or removed the
/// element), it looks among elements of the same kind of geometry that came
/// from the same place (the same name before any "#k", or for an edge, between
/// faces with the same names), and picks the one that looks most like the
/// signature (where its middle is, which way it faces, its size): Matched if
/// one clearly wins, Ambiguous if two are close. Failing that, an element only
/// partly related (an edge that kept one of its two faces) is Matched only if it
/// clearly looks like the signature; otherwise the reference is Missing. It
/// never guesses.
/// @code
/// const auto r = resolve(fillet.edges()[0], names);
/// if (r.found()) kernel.fillet(body, {r.index}, 2);
/// @endcode
/// @param ref The reference.
/// @param names The names of the result to look in.
/// @return Where it is now, and how sure that is.
Resolution resolve(const GeometryRef& ref, const ShapeNames& names);

/// The part of a name that says where it came from: the name without any
/// "#k" endings.
/// @param name A face, edge or vertex name.
/// @return The name with trailing "#1", "#2", ... removed.
std::string_view nameStem(std::string_view name);

/// The order to list a body's faces, edges or vertices in for people: by where
/// their middles are, bottom to top (z), then front to back (y), then left to
/// right (x). It doesn't depend on the kernel's own order, so the same design
/// lists the same way with any kernel version.
/// @param elements The body's elements.
/// @param kind Faces, edges or vertices.
/// @return Element numbers (from 0), in listing order.
std::vector<int> listingOrder(const kernel::Elements& elements, kernel::TopoKind kind);

/// Short text for a step's or sketch entity's id in names: its first 8 characters.
/// @param id The id's text (a UUID).
/// @return Its first 8 characters.
std::string shortId(const std::string& id);

/// Names every element of a result, given a name for each face. Faces that
/// share a name get "#1", "#2", ... (by where their middles are); then each edge
/// is named for the faces it borders, and each vertex for the faces around it.
/// @param elements The result's elements (from IKernel::elements()).
/// @param faceNames One name per face, in elements order.
/// @return The names.
ShapeNames nameFromFaces(kernel::Elements elements, std::vector<std::string> faceNames);

/// Gives a name for a face that came from something other than an input's face:
/// an input's edge or vertex (like a fillet's round), an outline curve, or an
/// extrude's start or end. Return an empty string to use a plain name for a new face.
using OriginNamer = std::function<std::string(const kernel::Origin&)>;

/// Names a result from its inputs' names and the kernel's history: each face
/// that came from an input's face keeps that face's name (faces merged into one
/// get their names joined by "+"); any other face is named by @p other, or
/// "<creator>/new" if that has nothing.
/// @param elements The result's elements.
/// @param history The kernel's history for the result.
/// @param inputs The inputs' names, in the kernel's input order (0, 1); nullptr where unnamed.
/// @param creator The step's short id (see shortId()).
/// @param other Names faces that didn't come from a face; may be empty.
/// @return The result's names.
ShapeNames carryNames(kernel::Elements elements, const kernel::History& history,
                      const std::vector<const ShapeNames*>& inputs, const std::string& creator,
                      const OriginNamer& other);

}  // namespace sb::model
