#pragma once

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

namespace kh::model {

enum class DocKind { Part, Assembly };

enum class FeatureStatus {
    Ok,          // built successfully
    Failed,      // the kernel could not build it
    Blocked,     // an earlier step it uses has a problem
    Suppressed,  // turned off by the user
};

struct FeatureResult {
    FeatureStatus status = FeatureStatus::Ok;
    kernel::ShapeHandle shape;
    Diagnostics issues;
};

// The outcome of rebuilding a document: one result per feature, in document order.
class RebuildReport final : public ResultLookup {
public:
    const FeatureResult* result(const FeatureId& id) const;
    const kernel::ShapeHandle* shapeOf(const FeatureId& id) const override;

    // Features whose result is a finished body: built successfully and not used
    // as an input by any later active step.
    const std::vector<FeatureId>& bodies() const { return bodies_; }

    // True if every active feature built successfully.
    bool ok() const;

private:
    friend class Document;
    std::vector<FeatureId> order_;
    std::map<FeatureId, FeatureResult> results_;
    std::vector<FeatureId> bodies_;
};

// One design file: an ordered list of features plus its undo history.
//
// Rule: only Commands change a Document. Read access is public; changes go
// through execute(), undo() and redo().
class Document {
public:
    explicit Document(DocKind kind = DocKind::Part, std::string name = "Untitled");
    ~Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    const DocId& id() const { return id_; }
    DocKind kind() const { return kind_; }
    const std::string& name() const { return name_; }

    // ---- Features (read only) ----
    std::size_t featureCount() const { return features_.size(); }
    const Feature& featureAt(std::size_t index) const { return *features_.at(index); }
    const Feature* find(const FeatureId& id) const;
    std::optional<std::size_t> indexOf(const FeatureId& id) const;
    // Features (suppressed ones too) that use `id` as an input.
    std::vector<FeatureId> dependents(const FeatureId& id) const;
    // A name not used by any feature yet: "Box1", "Box2", ...
    std::string uniqueName(const std::string& base) const;

    // ---- History ----
    // Applies a command. On success it joins the undo history (and clears redo).
    // On failure nothing changes and the errors are returned.
    Diagnostics execute(std::unique_ptr<Command> command);
    bool undo();
    bool redo();
    bool canUndo() const { return !done_.empty(); }
    bool canRedo() const { return !undone_.empty(); }
    std::string undoLabel() const;  // empty if nothing to undo
    std::string redoLabel() const;
    void setUndoLimit(std::size_t limit);  // oldest steps are dropped beyond this

    // ---- Saved state ----
    void markSaved();         // call after a successful save
    bool isModified() const;  // changed since the last markSaved()

    // ---- Rebuild ----
    RebuildReport regenerate(kernel::IKernel& kernel) const;

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

    std::vector<Entry> done_;
    std::vector<Entry> undone_;
    std::size_t undoLimit_ = 200;
    std::uint64_t nextSerial_ = 1;
    std::uint64_t savedSerial_ = 0;
};

}  // namespace kh::model
