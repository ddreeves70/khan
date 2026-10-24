#pragma once

// The parts of the 3D view that use OCCT's OpenGL classes (private to kh_view).
//
// They live in their own file because OCCT's OpenGL headers and Qt's (which
// bring in the system's gl.h) clash on macOS: no file may include both.

#include <Aspect_Drawable.hxx>
#include <Graphic3d_GraphicDriver.hxx>
#include <V3d_View.hxx>

namespace kh::view::detail {

// The one OCCT graphics driver every view shares, made the first time it's
// asked for; null if OCCT couldn't make it (no display). Never throws.
Handle(Graphic3d_GraphicDriver) sharedDriver();

// Gives `view` the OpenGL context that is current now (Qt's), drawing into a
// window `width` by `height` device pixels. `native` is the window to use, or
// 0 for the one the context was made current on (Windows). Can be called again
// when Qt makes a new context. Returns false if OCCT couldn't use the context.
bool attachContext(const Handle(V3d_View) & view, bool coreProfile, Aspect_Drawable native, int width,
                   int height);

// Makes OCCT draw into the framebuffer Qt has bound for this frame, and resizes
// the view to it. Call at the start of each frame. Returns false on failure.
bool useQtFramebuffer(const Handle(V3d_View) & view);

}  // namespace kh::view::detail
