#pragma once

#include <string>

#include "kh/core/Id.h"

namespace kh::model {

enum class DocKind { Part, Assembly };

// One design file. Placeholder for Phase 0, week 3, which adds the
// command stack (undo/redo), parameters and save/load.
//
// Rule: only Commands change a Document. UI, scripts and the AI
// assistant all go through the same command path.
class Document {
public:
    explicit Document(DocKind kind = DocKind::Part, std::string name = "Untitled");

    const DocId& id() const { return id_; }
    DocKind kind() const { return kind_; }
    const std::string& name() const { return name_; }

private:
    DocId id_;
    DocKind kind_;
    std::string name_;
};

}  // namespace kh::model
