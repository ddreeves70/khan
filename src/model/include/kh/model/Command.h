#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "kh/core/Diagnostic.h"
#include "kh/core/Id.h"
#include "kh/model/Feature.h"

namespace kh::model {

class Document;

// The only way to change a Document's contents. Commands receive a DocumentEditor
// from Document::execute/undo/redo; nothing else can create one. This enforces the
// rule that every change goes through the command stack, so undo always works.
class DocumentEditor {
public:
    const Document& document() const { return doc_; }

    Feature* find(const FeatureId& id);
    void insert(std::size_t index, std::unique_ptr<Feature> feature);
    std::unique_ptr<Feature> remove(const FeatureId& id);          // nullptr if not found
    std::unique_ptr<Feature> replace(std::unique_ptr<Feature> f);  // same id; returns the old one

private:
    friend class Document;
    explicit DocumentEditor(Document& doc) : doc_(doc) {}
    Document& doc_;
};

// One undoable change. Contract:
//  - apply() either makes the whole change and returns no errors, or returns
//    errors and leaves the document exactly as it was.
//  - revert() undoes a successful apply(); redo calls apply() again.
//  - Ids stay the same across undo and redo.
class Command {
public:
    virtual ~Command() = default;

    // Shown in Edit > Undo, for example "Add Box1".
    virtual std::string label() const = 0;

    virtual Diagnostics apply(DocumentEditor& editor) = 0;
    virtual void revert(DocumentEditor& editor) = 0;

    // Lets a burst of small changes (dragging a value) become one undo step.
    // Called on the newest command in history after `next` has been applied;
    // return true to absorb `next` into this command.
    virtual bool mergeWith(const Command& next) {
        (void)next;
        return false;
    }
};

}  // namespace kh::model
