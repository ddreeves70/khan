#pragma once

// The one OCCT graphics driver every view shares (private to kh_view).

#include <OpenGl_GraphicDriver.hxx>

namespace kh::view::detail {

// Made the first time it's asked for; null if OCCT couldn't make it (no display).
Handle(OpenGl_GraphicDriver) sharedDriver();

}  // namespace kh::view::detail
