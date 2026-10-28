#pragma once

/// @file
/// The list of open designs, and the one path for changing them.

#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "kh/core/Diagnostic.h"
#include "kh/core/Id.h"
#include "kh/kernel/IKernel.h"
#include "kh/model/CommandRegistry.h"
#include "kh/model/Document.h"
#include "kh/plugin/EventBus.h"
#include "kh/plugin/Events.h"

namespace kh::plugin {

/// The designs open in Khan.
///
/// Plugins read designs here and change them only through execute(), run(),
/// undo() and redo(). After every change the list sends DocumentChanged, rebuilds
/// the design (when it has a kernel) and sends RebuildFinished, so every panel
/// and view hears about it no matter which plugin made the change.
///
/// @code
/// DocId id = docs.add(std::make_unique<model::Document>());
/// docs.run(id, "feature.add_box", {{"size", kernel::Vec3{10, 20, 30}}});
/// const model::RebuildReport* built = docs.lastRebuild(id);
/// @endcode
class DocumentList {
public:
    /// Makes an empty list.
    /// @param bus Where change events are sent. Must outlive the list.
    /// @param commands The commands run() can find by name. Must outlive the list.
    /// @param kernel The kernel used to rebuild designs after each change; null
    ///        means designs are not rebuilt and RebuildFinished is not sent.
    /// @param solver The solver for Sketch steps; null means Sketch steps fail to rebuild.
    DocumentList(EventBus& bus, const model::CommandRegistry& commands, kernel::IKernel* kernel = nullptr,
                 sketch::ISketchSolver* solver = nullptr);

    /// Closes every design.
    ~DocumentList();

    DocumentList(const DocumentList&) = delete;
    DocumentList& operator=(const DocumentList&) = delete;

    /// Adds a new or opened design. Sends DocumentOpened, then rebuilds it. The
    /// first design added becomes the active one.
    /// @param doc The design. The list takes ownership.
    /// @return The design's id.
    DocId add(std::unique_ptr<model::Document> doc);

    /// Closes a design. Sends DocumentClosed; if it was active, the next open
    /// design (or none) becomes active.
    /// @param id The design to close.
    /// @return False if no open design has that id.
    bool close(const DocId& id);

    /// Lists the open designs.
    /// @return Their ids, in the order they were added.
    std::vector<DocId> ids() const;

    /// Finds an open design, for reading.
    /// @param id The design.
    /// @return The design, or nullptr if it is not open.
    const model::Document* find(const DocId& id) const;

    /// Gives the design the student is working on.
    /// @return Its id, or a nil id if nothing is open.
    DocId active() const { return active_; }

    /// Makes another open design the active one. Sends ActiveDocumentChanged.
    /// @param id The design.
    /// @return False if it is not open.
    bool setActive(const DocId& id);

    /// Runs a command on a design. On success sends DocumentChanged and rebuilds.
    /// @param id The design.
    /// @param command The command. The design takes ownership.
    /// @return No diagnostics on success; otherwise why it failed, and nothing changed.
    Diagnostics execute(const DocId& id, std::unique_ptr<model::Command> command);

    /// Runs a command by name, for menus, scripts and the assistant.
    /// @param id The design.
    /// @param name The command name, e.g. "feature.add_box".
    /// @param args Its arguments.
    /// @return No diagnostics on success; otherwise every problem found.
    Diagnostics run(const DocId& id, std::string_view name, const model::Args& args);

    /// Undoes the newest step of a design. On success sends DocumentChanged and rebuilds.
    /// @param id The design.
    /// @return False if it is not open or has nothing to undo.
    bool undo(const DocId& id);

    /// Redoes the newest undone step of a design. On success sends DocumentChanged and rebuilds.
    /// @param id The design.
    /// @return False if it is not open or has nothing to redo.
    bool redo(const DocId& id);

    /// Records that a design was just saved, so it shows no unsaved changes.
    /// @param id The design.
    /// @return False if it is not open.
    bool markSaved(const DocId& id);

    /// Gives the result of a design's latest rebuild.
    /// @param id The design.
    /// @return The report, or nullptr if the design is not open or there is no kernel.
    const model::RebuildReport* lastRebuild(const DocId& id) const;

    /// Gives the kernel designs are rebuilt with, for questions about shapes
    /// such as their volume (see model::bodyMass()).
    /// @return The kernel, or nullptr if there is none (designs aren't rebuilt).
    const kernel::IKernel* kernel() const { return kernel_; }

private:
    struct Entry {
        std::unique_ptr<model::Document> doc;
        std::optional<model::RebuildReport> rebuild;
    };
    Entry* entry(const DocId& id);
    void changed(Entry& e, ChangeKind kind, std::string label);
    void rebuild(Entry& e);

    EventBus& bus_;
    const model::CommandRegistry& commands_;
    kernel::IKernel* kernel_;
    sketch::ISketchSolver* solver_;
    std::vector<DocId> order_;
    std::map<DocId, Entry> docs_;
    DocId active_;
};

}  // namespace kh::plugin
