#pragma once

/// @file
/// The plugin API version and the macros an outside plugin uses to export itself.

namespace sb::plugin {

class PluginContext;

/// Version of the plugin API in this build of SketchBillet.
///
/// A plugin's `plugin.json` gives the `apiVersion` it was written for, and an
/// outside C++ plugin also reports the version it was compiled against. Both
/// must equal this number, or SketchBillet refuses to load the plugin with a message.
/// The number goes up whenever a change could break an existing plugin.
/// Version 2 added actions, the shared selection and services. Version 3 added
/// panels the student can open more than once (PanelSpec::allowMultiple).
/// Version 4 lets plugins share things: services offered by plugins, onStart(),
/// publish(), and "uses" in plugin.json. Version 5 added settings(), actions
/// that run plugin code with a check mark, submenus, and status-bar items.
/// Version 6 added overlays() and moved the grid out of sb_plugin into the
/// sb.grid plugin (its sb.grid/1 service). Version 7 added tiers ("tier" and
/// "tiers" in plugin.json, and in PluginManifest). Version 8 added Labels
/// overlays (words drawn in the viewers). Version 9 added panels that start
/// closed (PanelSpec::startHidden) and UiRegistry::showPanel(). Version 10
/// added overlays for one pane (Overlay::pane, PluginOverlays::forPane()).
/// Version 11 added command arguments that take several steps
/// (model::ArgType::FeatureList) and actions that use every selected step
/// (ActionSpec::fromSelectionList). Version 12 added folders: command
/// arguments that name a folder (model::ArgType::FolderRef). Version 13 added
/// sketches: Sketch steps (model::SketchFeature), points on a sketch's plane as
/// command arguments (model::ArgType::Vec2), the solved sketch in a rebuild
/// result (model::FeatureResult::sketch), and the sketch solver given to
/// PluginHost and DocumentList. Version 14 added menu items that are enabled
/// only when the plugin says so (ActionSpec::enabled) and the ActionsChanged event,
/// toolbar modes (ActionSpec::toolbarMode, PluginContext::setToolbarMode(),
/// ToolbarModeChanged), and shorter words and pictures for toolbar buttons
/// (ActionSpec::buttonText, ActionSpec::icon). Version 15 gave plugins the
/// sketch solver (DocumentList::solver()), dragging (sketch::ISketchSolver::drag())
/// and the parts of a solved sketch that can still move (sketch::SolveResult::
/// freePoints, freeEntities). Version 16 added toolbar buttons that show only
/// while their item can be used (ActionSpec::buttonWhenEnabled). Version 17 added
/// Extrude steps (model::ExtrudeFeature), steps that use sketches
/// (model::Feature::sketchInputs(), model::ResultLookup::resultOf(), the sketch's
/// plane in model::FeatureResult::plane), and faces and extrude in the kernel
/// (kernel::IKernel::makeFace(), kernel::IKernel::extrude()). Version 18 added
/// picking a design's settings (Selection::pickDesign(), SelectionChanged::design),
/// panels that float over the 3D view (ui::PanelSpec::floatsOverView), closing a
/// panel (ui::UiRegistry::hidePanel()) and SVG
/// icons for plugins' own widgets (ui::UiRegistry::icon()). Version 19 added the
/// unit a design shows lengths in (model::Document::lengthUnit(), sb::LengthUnit
/// in sb/core/Units.h, model::SetUnitsCommand and the document.set_units command).
/// Version 20 added splines: sketch::SplineShape in a sketch's shapes, its control
/// points (sketch::PointRole::Pole and PointRef::index, sketch::pointsOf(),
/// pointKey(), splinePoint()), kernel::Spline2 edges for faces, lists of points as
/// command arguments (model::ArgType::Vec2List) and the sketch.add_spline command.
/// Version 21 added overlays drawn on the view cube (Overlay::onViewCube,
/// kViewCubeHalfSize). Version 22 added splines through points
/// (sketch::splinePolesThrough(), splineThroughAt(), and sketch.add_spline's
/// `through`) and the commands that change a spline: sketch.set_spline,
/// sketch.set_weight, sketch.add_pole and sketch.remove_pole. Version 23 added
/// constraints on splines (sketch::ConstraintKind::PointOnSpline and Smooth, and
/// Tangent where a spline's end is joined to a curve) and their helpers
/// (sketch::isEnd(), jointOf(), curveEnd(), curvatureVector(), nearestOnSpline()).
/// Version 24 added splines from a file of points (sketch::readPoints(),
/// SplineShape::source, and the sketch.import_spline and sketch.reload_spline
/// commands) and asking the student for a file (ui::UiRegistry::askOpenFile()).
/// Version 25 added rectangles (model::addRectangle() and sketch.add_rectangle).
/// Version 26 added rounding corners (model::roundCorner(), largestCornerRadius()
/// and sketch.round_corner). Version 27 added trimming (model::trim(),
/// trimmedPiece() and sketch.trim). Version 28 added the Symmetric constraint
/// (sketch::ConstraintKind::Symmetric). Version 29 added parameters
/// (model::Expression, model::ParameterTable, Document::parameters(), and the
/// parameter.set, parameter.rename and parameter.delete commands). Version 30
/// added dimensions worked out from formulas (sketch::Constraint::expression,
/// sb/model/Formulas.h, and formula= on sketch.constrain and sketch.set_value).
/// Version 31 added steps' numbers by name, with formulas (Feature::numbers(),
/// number(), setNumber(), formulas(), SetNumberCommand and feature.set_value).
/// Version 32 added handles to drag in the viewers (sb.viewer/3:
/// viewer::Handle, IHandleUser, IViewer::setHandles()) and sliding bodies
/// (model::SlideBodyCommand, feature.slide).
/// Version 33 is the rename from Khan to SketchBillet: namespaces sb (was kh),
/// headers sb/... (was kh/...), libraries sb_... and plugin ids sb.... (was
/// kh....). Rebuild outside plugins against it.
inline constexpr int kPluginApiVersion = 33;

/// The function every plugin provides. SketchBillet calls it once, at start, to let the
/// plugin add its commands and start listening to events.
/// @param ctx The plugin's door into SketchBillet. It stays valid until SketchBillet closes.
using RegisterFn = void (*)(PluginContext& ctx);

}  // namespace sb::plugin

/// Marks a function as visible from outside a plugin's shared library.
#if defined(_WIN32)
#define SB_PLUGIN_EXPORT __declspec(dllexport)
#else
#define SB_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/// Turns a function into the entry point of an outside C++ plugin.
///
/// Put this once in the plugin's code, after the function. It exports the two
/// functions SketchBillet looks for in the shared library: `sb_plugin_api_version`, so
/// SketchBillet can check the plugin was built for this API, and `sb_plugin_register`,
/// which calls @p fn.
///
/// @code
/// void registerHello(sb::plugin::PluginContext& ctx) { ... }
/// SB_PLUGIN_ENTRY(registerHello)
/// @endcode
/// @param fn A function `void fn(sb::plugin::PluginContext&)`.
#define SB_PLUGIN_ENTRY(fn)                                                                 \
    extern "C" SB_PLUGIN_EXPORT int sb_plugin_api_version() {                               \
        return ::sb::plugin::kPluginApiVersion;                                             \
    }                                                                                       \
    extern "C" SB_PLUGIN_EXPORT void sb_plugin_register(::sb::plugin::PluginContext& ctx) { \
        fn(ctx);                                                                            \
    }
