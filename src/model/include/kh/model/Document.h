#pragma once

/// @file
/// The Document: a design's features, its undo history and rebuilds.

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "kh/core/Diagnostic.h"
#include "kh/core/Id.h"
#include "kh/kernel/IKernel.h"
#include "kh/model/Command.h"
#include "kh/model/Feature.h"
#include "kh/model/Folder.h"
#include "kh/model/Material.h"
#include "kh/sketch/Solver.h"

namespace kh::model {

/// What kind of design a Document holds.
enum class DocKind {
    Part,      ///< A single part made of features.
    Assembly,  ///< Parts put together with mates (Phase 3).
};

/// How one feature came out of a rebuild.
enum class FeatureStatus {
    Ok,          ///< Built successfully.
    Failed,      ///< The kernel could not build it.
    Blocked,     ///< An earlier step it uses has a problem or is turned off.
    Suppressed,  ///< Turned off by the user.
};

/// The rebuild result of one feature.
struct FeatureResult {
    FeatureStatus status = FeatureStatus::Ok;  ///< How it came out.
    kernel::ShapeHandle shape;                 ///< The shape; empty unless status is Ok.
    Diagnostics issues;                        ///< Why it failed or is blocked.
    /// For a Sketch step: what the solver gave back (the solved geometry, the
    /// degrees of freedom, conflicting and repeated constraints). Null otherwise.
    std::shared_ptr<const sketch::SolveResult> sketch;
};

/// The outcome of rebuilding a document: one result per feature, in document order.
class RebuildReport final : public ResultLookup {
public:
    /// Finds one feature's result.
    /// @param id The feature to look up.
    /// @return The result, or nullptr if the feature was not part of the rebuild.
    const FeatureResult* result(const FeatureId& id) const;

    const kernel::ShapeHandle* shapeOf(const FeatureId& id) const override;

    /// Lists the finished bodies: features that built successfully and are not
    /// used as an input by any later active step.
    /// @return The body features, in document order.
    const std::vector<FeatureId>& bodies() const { return bodies_; }

    /// Checks whether everything built.
    /// @return True if every active (not suppressed) feature built successfully.
    bool ok() const;

private:
    friend class Document;
    std::vector<FeatureId> order_;
    std::map<FeatureId, FeatureResult> results_;
    std::vector<FeatureId> bodies_;
};

/// One design file: an ordered list of features plus its undo history.
///
/// Rule: only Commands change a Document. Read access is public; changes go
/// through execute(), undo() and redo().
///
/// @code
/// Document doc;
/// doc.execute(std::make_unique<AddFeatureCommand>(
///     std::make_unique<BoxFeature>("Box1", kernel::Vec3{}, kernel::Vec3{10, 10, 10})));
/// doc.undo();
/// doc.redo();
/// RebuildReport report = doc.regenerate(kernel);
/// @endcode
class Document {
public:
    /// Makes an empty document with a new id.
    /// @param kind Part or assembly.
    /// @param name The document's name, shown in the title bar.
    explicit Document(DocKind kind = DocKind::Part, std::string name = "Untitled");

    /// Makes an empty document with a given id, for loading a saved design.
    /// @param id The id it was saved with.
    /// @param kind Part or assembly.
    /// @param name The document's name, shown in the title bar.
    Document(DocId id, DocKind kind, std::string name);

    /// Destroys the document and its history.
    ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    /// Gives the document's id, which never changes.
    /// @return The id.
    const DocId& id() const { return id_; }

    /// Gives the kind of design.
    /// @return Part or assembly.
    DocKind kind() const { return kind_; }

    /// Gives the document's name.
    /// @return The name.
    const std::string& name() const { return name_; }

    /// Counts the features.
    /// @return The number of features, suppressed ones included.
    std::size_t featureCount() const { return features_.size(); }

    /// Gives a feature by position.
    /// @param index Position from 0 to featureCount() - 1.
    /// @return The feature. Throws std::out_of_range if @p index is too large
    ///         (a programming error, not a user error).
    const Feature& featureAt(std::size_t index) const { return *features_.at(index); }

    /// Finds a feature by id.
    /// @param id The feature to find.
    /// @return The feature, or nullptr if it is not in the document.
    const Feature* find(const FeatureId& id) const;

    /// Finds a feature's position.
    /// @param id The feature to find.
    /// @return Its position, or std::nullopt if it is not in the document.
    std::optional<std::size_t> indexOf(const FeatureId& id) const;

    /// Lists the features (suppressed ones too) that use a feature as an input.
    /// @param id The feature that may be used.
    /// @return The ids of the features that use it, in document order.
    std::vector<FeatureId> dependents(const FeatureId& id) const;

    /// Gives the materials this design added to Khan's built-in ones.
    /// @return The design's own materials, in the order they were added.
    const std::vector<Material>& customMaterials() const { return materials_; }

    /// Finds a material by key or name (ignoring upper and lower case): the
    /// design's own materials first, then Khan's built-in ones.
    /// @param keyOrName For example "pla", "Aluminum 6061", or a custom material's name.
    /// @return The material, or nullptr if there is none by that key or name.
    const Material* findMaterial(std::string_view keyOrName) const;

    /// Works out what a step's body is made of: the material picked for it; if
    /// none, the material of the body it was made from (the first body, A, of a
    /// union, cut or intersect, or the body a Move moves), and so on back; and if
    /// none was picked anywhere,
    /// defaultMaterial() (PLA). A key that names no material counts as none.
    /// @param id The step.
    /// @return The material. For a step not in the design, defaultMaterial().
    const Material& materialOf(const FeatureId& id) const;

    /// Finds the step whose picked material materialOf() uses.
    /// @param id The step.
    /// @return That step (@p id itself, or one it was made from), or a nil id if
    ///         the body has the default material.
    FeatureId materialSource(const FeatureId& id) const;

    /// Lists the design's folders, in the order they appear in the feature tree.
    /// Each folder lists only the steps still in the design; a folder whose steps
    /// are all gone (deleted, until undo brings them back) isn't listed.
    /// @return The folders.
    std::vector<Folder> folders() const;

    /// Finds a folder by id.
    /// @param id The folder.
    /// @return It, as folders() lists it, or nullptr if it isn't listed there.
    const Folder* findFolder(const FolderId& id) const;

    /// Finds a folder by name (exact).
    /// @param name For example "Holes".
    /// @return It, as folders() lists it, or nullptr if there's none by that name.
    const Folder* findFolder(std::string_view name) const;

    /// Finds the folder a step is in.
    /// @param id The step.
    /// @return The folder's id, or a nil id if the step isn't in one.
    FolderId folderOf(const FeatureId& id) const;

    /// Makes a name that no folder uses yet.
    /// @param base The start of the name, e.g. "Folder".
    /// @return The first free name of "Folder1", "Folder2", ...
    std::string uniqueFolderName(const std::string& base) const;

    /// Makes a name that no feature uses yet.
    /// @param base The start of the name, e.g. "Box".
    /// @return The first free name of "Box1", "Box2", ...
    std::string uniqueName(const std::string& base) const;

    /// Applies a command. On success it joins the undo history and clears redo.
    /// @param command The command to run. The document takes ownership.
    /// @return No diagnostics on success. On failure, the errors, and nothing changed.
    Diagnostics execute(std::unique_ptr<Command> command);

    /// Undoes the newest step.
    /// @return False if there is nothing to undo.
    bool undo();

    /// Redoes the newest undone step.
    /// @return False if there is nothing to redo.
    bool redo();

    /// Checks whether undo() would do something.
    /// @return True if there is a step to undo.
    bool canUndo() const { return !done_.empty(); }

    /// Checks whether redo() would do something.
    /// @return True if there is a step to redo.
    bool canRedo() const { return !undone_.empty(); }

    /// Gives the label for Edit > Undo.
    /// @return The newest step's label, e.g. "Add Box1", or empty if there is nothing to undo.
    std::string undoLabel() const;

    /// Gives the label for Edit > Redo.
    /// @return The next redo step's label, or empty if there is nothing to redo.
    std::string redoLabel() const;

    /// Sets how many steps undo keeps. The oldest steps are dropped beyond this.
    /// @param limit The number of steps (at least 1; the default is 200).
    void setUndoLimit(std::size_t limit);

    /// Forgets all undo and redo steps; the features stay as they are. Used after
    /// opening a file, so "undo" can't take apart a design that was just loaded.
    void clearHistory();

    /// Records that the document was just saved. Call after a successful save.
    void markSaved();

    /// Checks for unsaved changes.
    /// @return True if the document differs from the last markSaved() (undo back
    ///         to the saved state counts as unchanged).
    bool isModified() const;

    /// Rebuilds every feature in order.
    ///
    /// Suppressed features are skipped. A feature whose input has no shape is
    /// Blocked and never reaches the kernel. The document itself is not changed.
    /// @param kernel The geometry kernel to build with.
    /// @param solver The sketch solver for Sketch steps. Without one, Sketch steps fail.
    /// @return One result per feature, plus the finished bodies.
    RebuildReport regenerate(kernel::IKernel& kernel, sketch::ISketchSolver* solver = nullptr) const;

private:
    friend class DocumentEditor;

    struct Entry {
        std::unique_ptr<Command> command;
        std::uint64_t serial;  // identifies the document state this entry produces
    };

    std::uint64_t currentSerial() const { return done_.empty() ? 0 : done_.back().serial; }

    DocId id_;
    DocKind kind_;
    std::string name_;
    std::vector<std::unique_ptr<Feature>> features_;
    std::vector<Material> materials_;  // the design's own materials
    std::vector<Folder> folders_;      // as the commands left them; folders() hides deleted steps
    std::vector<Folder> shown_;        // folders(), kept so findFolder() can hand out pointers
    void updateFolders();              // recomputes shown_ after every change

    std::vector<Entry> done_;
    std::vector<Entry> undone_;
    std::size_t undoLimit_ = 200;
    std::uint64_t nextSerial_ = 1;
    std::uint64_t savedSerial_ = 0;
};

}  // namespace kh::model
