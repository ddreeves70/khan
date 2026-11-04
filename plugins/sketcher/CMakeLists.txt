# sb.sketcher: draws sketches in the viewers, and edits them with the mouse
# (Line, Circle, Arc, Point; Select, drag) through the viewer's drawing tools
# (sb.viewer/3), and adds constraints and dimensions to what's picked.
# Built only with the desktop app.
sb_add_library(sb_plugin_sketcher
  SOURCES
    SketcherPlugin.cpp
    Constrain.cpp
    Constrain.h
    SketchPanel.cpp
    SketchPanel.h
  DEPS
    sb::ui
    sb::plugin
    sb::model
    sb::plugin_viewer
    sb::plugin_grid
)
sb_embed_files(sb_plugin_sketcher HEADER SketcherIcons.h NAMESPACE sb::icons::sketcher
  FILES icons/new_sketch.svg icons/edit_sketch.svg icons/finish_sketch.svg icons/select.svg
        icons/coincident.svg icons/horizontal.svg icons/vertical.svg icons/parallel.svg icons/perpendicular.svg icons/equal.svg icons/tangent.svg icons/smooth.svg icons/on.svg icons/midpoint.svg icons/symmetric.svg icons/fix.svg icons/dimension.svg icons/horizontal_dimension.svg icons/vertical_dimension.svg
        icons/line.svg icons/rectangle.svg icons/round_corner.svg icons/trim.svg icons/circle.svg icons/arc.svg icons/point.svg icons/spline.svg icons/spline_through.svg)
