#pragma once

/// @file
/// The event types sent on the EventBus. They are part of the plugin API.

#include <string>
#include <string_view>
#include <vector>

#include "kh/core/Id.h"

/// The plugin system: manifests, the plugin host, the event bus and open documents.
namespace kh::plugin {

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
    static constexpr std::string_view kType = "kh.DocumentChanged";  ///< Stable event type name.
    DocId doc;                                                       ///< The document that changed.
    ChangeKind kind = ChangeKind::Executed;                          ///< What happened.
    std::string label;                                               ///< The step's label, e.g. "Add Box1".
};

/// A design was rebuilt after a change. Look up the result with DocumentList::lastRebuild().
struct RebuildFinished {
    static constexpr std::string_view kType = "kh.RebuildFinished";  ///< Stable event type name.
    DocId doc;                                                       ///< The document that was rebuilt.
    bool ok = true;                                                  ///< True if every active step built.
};

/// A design was added to the list of open documents (made new or opened from a file).
struct DocumentOpened {
    static constexpr std::string_view kType = "kh.DocumentOpened";  ///< Stable event type name.
    DocId doc;                                                      ///< The document.
};

/// A design was closed. Its id is no longer in the DocumentList.
struct DocumentClosed {
    static constexpr std::string_view kType = "kh.DocumentClosed";  ///< Stable event type name.
    DocId doc;                                                      ///< The document.
};

/// Another design became the one the student is working on.
struct ActiveDocumentChanged {
    static constexpr std::string_view kType = "kh.ActiveDocumentChanged";  ///< Stable event type name.
    DocId doc;  ///< The new active document; nil if none is open.
};

/// The shared selection changed.
///
/// Until faces and edges can be picked (Phase 2, GeometryRef), a selection is a
/// list of steps.
struct SelectionChanged {
    static constexpr std::string_view kType = "kh.SelectionChanged";  ///< Stable event type name.
    DocId doc;                                                        ///< The document the selection is in.
    std::vector<FeatureId> features;  ///< The selected steps, in the order they were picked.
};

/// A plugin's remembered setting changed (see PluginSettings).
struct SettingChanged {
    static constexpr std::string_view kType = "kh.SettingChanged";  ///< Stable event type name.
    std::string plugin;  ///< The id of the plugin the setting belongs to.
    std::string key;     ///< The setting's name, e.g. "unit".
};

/// Send it when your menu items should be enabled, checked or worded differently
/// for a reason Khan can't see (your tool started or stopped, say). Khan then asks
/// every item's `enabled`, `checked` and `currentText` again. It already asks after
/// design, selection and setting changes, and when a menu opens.
struct ActionsChanged {
    static constexpr std::string_view kType = "kh.ActionsChanged";  ///< Stable event type name.
};

}  // namespace kh::plugin
