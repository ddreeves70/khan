#pragma once

/// @file
/// Starting 3D graphics (OpenGL) for the views, and finding out if it works.

#include <QString>

namespace kh::view {

/// Gets Qt ready for 3D views. Call it once in main(), before making the
/// QApplication: views share their OpenGL resources, and on Linux the views
/// draw through X11 (Wayland desktops run it through XWayland).
///
/// @code
/// int main(int argc, char** argv) {
///     kh::view::prepareGraphics();
///     QApplication app(argc, argv);
///     if (QString why = kh::view::graphicsProblem(); !why.isEmpty()) {
///         // show the flat view and `why` instead of ViewWidget
///     }
/// }
/// @endcode
void prepareGraphics();

/// Checks whether 3D views can work here. Call it after the QApplication is made.
///
/// It starts the shared OCCT graphics driver the first time it is called, so
/// the answer is cached. A view can still fail later, when it first draws, if
/// the computer's OpenGL is too old; ViewWidget says so then.
/// @return Empty if 3D views can work, otherwise a plain sentence saying why
///         not and what to do.
QString graphicsProblem();

}  // namespace kh::view
