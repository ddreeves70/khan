#pragma once

/// @file
/// The Command interface and the editor that commands use to change a Document.

#include <cstddef>
#include <memory>
#include <string>

#include "kh/core/Diagnostic.h"
#include "kh/core/Id.h"
#include "kh/model/Feature.h"
#include "kh/model/Folder.h"
#include "kh/model/Material.h"

namespace kh::model {

class Document;

/// Write access to a Document, given only to Commands.
///
/// Commands receive a DocumentEditor from Document::execute(), Document::undo()
/// and Document::redo(); nothing else can create one. This enforces the rule
/// that every change goes through the command stack, so undo always works.
class DocumentEditor {
public:
    /// Gives read access to the document being changed.
    /// @return The document.
    const Document& document() const { return doc_; }

    /// Finds a feature to change in place.
    /// @param id The feature to find.
    /// @return The feature, or nullptr if it is not in the document.
    Feature* find(const FeatureId& id);

    /// Inserts a feature.
    /// @param index Position in the feature list; past the end means append.
    /// @param feature The feature to insert. The document takes ownership.
    void insert(std::size_t index, std::unique_ptr<Feature> feature);

    /// Removes a feature.
    /// @param id The feature to remove.
    /// @return The removed feature (the caller now owns it), or nullptr if not found.
    std::unique_ptr<Feature> remove(const FeatureId& id);

    /// Swaps a feature for another with the same id, at the same position.
    /// @param f The new version. Its id must already be in the document.
    /// @return The old version, or nullptr if no feature has that id.
    std::unique_ptr<Feature> replace(std::unique_ptr<Feature> f);

    /// Gives the design's own materials to change in place.
    /// @return The list (see Document::customMaterials()).
    std::vector<Material>& materials();

    /// Gives the design's folders to change in place, as commands left them
    /// (with steps that were deleted still listed; see Document::folders()).
    /// @return The list.
    std::vector<Folder>& folders();

private:
    friend class Document;
    explicit DocumentEditor(Document& doc) : doc_(doc) {}
    Document& doc_;
};

/// One undoable change to a Document.
///
/// Contract:
///  - apply() either makes the whole change and returns no errors, or returns
///    errors and leaves the document exactly as it was.
///  - revert() undoes a successful apply(); redo calls apply() again.
///  - Ids stay the same across undo and redo.
class Command {
public:
    /// Destroys the command.
    virtual ~Command() = default;

    /// Gives the text shown in Edit > Undo.
    /// @return A short label, for example "Add Box1".
    virtual std::string label() const = 0;

    /// Makes the change.
    /// @param editor Write access to the document.
    /// @return No diagnostics on success. On failure, the errors, and the document is unchanged.
    virtual Diagnostics apply(DocumentEditor& editor) = 0;

    /// Undoes a successful apply().
    /// @param editor Write access to the document.
    virtual void revert(DocumentEditor& editor) = 0;

    /// Lets a burst of small changes (dragging a value) become one undo step.
    ///
    /// Called on the newest command in history after @p next has been applied.
    /// @param next The command that was just applied.
    /// @return True to absorb @p next into this command; false to keep them separate (the default).
    virtual bool mergeWith(const Command& next) {
        (void)next;
        return false;
    }
};

}  // namespace kh::model
