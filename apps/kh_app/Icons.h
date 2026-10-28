#pragma once

// Icons for menu items and toolbar buttons, drawn from SVG text.

#include <QIcon>

#include <string_view>

namespace kh::app {

// Makes an icon from SVG text (an ActionSpec::icon, or one of the app's own),
// drawn sharp at 16 to 48 px on ordinary and high-DPI screens. "currentColor" in
// the SVG is the theme's text color, so lines stay visible on light and dark
// themes. Returns a null QIcon if the text isn't SVG that Qt can draw.
QIcon svgIcon(std::string_view svg);

}  // namespace kh::app
