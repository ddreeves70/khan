#pragma once

/// @file
/// A folder in a design's feature tree: a name for a run of steps next to each other.

#include <string>
#include <vector>

#include "kh/core/Id.h"

namespace kh::model {

/// A named folder holding steps that are next to each other in the feature tree.
///
/// Folders are only for keeping the tree tidy: they never change what's built.
/// A step is in at most one folder. Folders are changed only by commands
/// (AddFolderCommand, RenameFolderCommand, DeleteFolderCommand).
struct Folder {
    FolderId id{};                 ///< Never changes, even when the folder is renamed.
    std::string name{};            ///< What the student sees, e.g. "Holes". No two folders share a name.
    std::vector<FeatureId> steps;  ///< Its steps, in design order. Never empty in Document::folders().

    /// Compares two folders, for tests.
    /// @param other The other folder.
    /// @return True if the id, name and steps are the same.
    bool operator==(const Folder& other) const = default;
};

}  // namespace kh::model
