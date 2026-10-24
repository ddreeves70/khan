#pragma once

/// @file
/// Reaching the OCCT shape behind a ShapeHandle, for code that draws with OCCT.

#include "kh/kernel/Types.h"

class TopoDS_Shape;

namespace kh::kernel {

/// Gives the OCCT shape behind a handle made by the OCCT kernel.
///
/// Only code that is allowed to use OCCT types (the 3D viewer, kh_view) calls
/// this; everything else keeps working with ShapeHandle. The shape lives as
/// long as the handle (or a copy of it) does.
/// @param shape A handle from makeOcctKernel().
/// @return The shape, or nullptr if the handle is empty or came from another kernel.
const TopoDS_Shape* occtShape(const ShapeHandle& shape);

}  // namespace kh::kernel
