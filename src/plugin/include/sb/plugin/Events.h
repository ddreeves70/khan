#pragma once

/// @file
/// The event types sent on the EventBus. They are part of the plugin API.

#include <string>
#include <string_view>
#include <vector>

#include "sb/core/Id.h"
#include "sb/plugin/Picks.h"

/// The plugin system: manifests, the plugin host, the event bus and open documents.
namespace sb::plugin {

// Every event is a plain struct with a stable type name in kType. Events carry
// ids, never pointers: a listener that needs more looks it up, for example with
// DocumentList::find(). Events only report; a listener that wants to change the
// design runs a command.

/// How a document changed.
enum class ChangeKind {
    Executed,  ///< A command ran.
    Undone,    ///< A step was undone.
    Redone,    ///< A step was redone.
};

/// A design changed through its command stack.
struct DocumentChanged {
    static constexpr std::string_view kType = "sb.DocumentChanged";  ///< Stable event type name.
    DocId doc;                                                       ///< The document that changed.
    ChangeKind kind = ChangeKind::Executed;                          ///< What happened.
    std::string label;                                               ///< The step's label, e.g. "Add Box1".
};

/// A design was rebuilt after a change. Look up the result with DocumentList::lastRebuild().
struct RebuildFinished {
    static constexpr std::string_view kType = "sb.RebuildFinished";  ///< Stable event type name.
    DocId doc;                                                       ///< The document that was rebuilt.
    bool ok = true;                                                  ///< True if every active step built.
};

/// A design was added to the list of open documents (made new or opened from a file).
struct DocumentOpened {
    static constexpr std::string_view kType = "sb.DocumentOpened";  ///< Stable event type name.
    DocId doc;                                                      ///< The document.
};

/// A design was closed. Its id is no longer in the DocumentList.
struct DocumentClosed {
    static constexpr std::string_view kType = "sb.DocumentClosed";  ///< Stable event type name.
    DocId doc;                                                      ///< The document.
};

/// Another design became the one the student is working on.
struct ActiveDocumentChanged {
    static constexpr std::string_view kType = "sb.ActiveDocumentChanged";  ///< Stable event type name.
    DocId doc;  ///< The new active document; nil if none is open.
};

/// The shared selection changed: steps, the design itself, or edges (see Selection).
struct SelectionChanged {
    static constexpr std::string_view kType = "sb.SelectionChanged";  ///< Stable event type name.
    DocId doc;                                                        ///< The document the selection is in.
    std::vector<FeatureId> features;  ///< The selected steps, in the order they were picked.
    bool design =
        false;  ///< The design itself is picked (Document settings), not steps; see Selection::pickDesign().
    std::vector<PickedElement> elements;  ///< The picked edges, when edges are picked instead of steps.
};

/// What clicking a body in a viewer picks changed (see Selection::setPickMode()).
struct PickModeChanged {
    static constexpr std::string_view kType = "sb.PickModeChanged";  ///< Stable event type name.
    PickMode mode = PickMode::Bodies;                                ///< What clicking picks now.
};

/// A plugin's remembered setting changed (see PluginSettings).
struct SettingChanged {
    static constexpr std::string_view kType = "sb.SettingChanged";  ///< Stable event type name.
    std::string plugin;  ///< The id of the plugin the setting belongs to.
    std::string key;     ///< The setting's name, e.g. "unit".
};

/// The toolbar switched to another mode (see PluginContext::setToolbarMode()).
struct ToolbarModeChanged {
    static constexpr std::string_view kType = "sb.ToolbarModeChanged";  ///< Stable event type name.
    std::string mode;  ///< The mode now on, e.g. "sb.sketcher.editing"; empty for the usual toolbar.
};

/// Send it when your menu items should be enabled, checked or worded differently
/// for a reason SketchBillet can't see (your tool started or stopped, say). SketchBillet then asks
/// every item's `enabled`, `checked` and `currentText` again. It already asks after
/// design, selection and setting changes, and when a menu opens.
struct ActionsChanged {
    static constexpr std::string_view kType = "sb.ActionsChanged";  ///< Stable event type name.
};

}  // namespace sb::plugin
