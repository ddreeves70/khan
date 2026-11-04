#pragma once

/// @file
/// Saving and opening .sb design files.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include "sb/core/Diagnostic.h"
#include "sb/model/Document.h"

/// Reading and writing files: the native .sb format.
namespace sb::io {

/// The bytes of a whole file in memory.
using Bytes = std::vector<std::uint8_t>;

/// What openFile() and readBytes() return.
struct OpenResult {
    std::unique_ptr<model::Document> document;  ///< The design; null if it could not be opened.
    Diagnostics issues;                         ///< Why it could not be opened, in plain words.

    /// Checks whether the design was opened.
    /// @return True if @ref document is set.
    bool ok() const { return document != nullptr; }
};

/// Saves a design as a .sb file and marks the document as saved.
///
/// The file is written to a temporary file first and then moved into place, so
/// a failed save never damages an existing file.
///
/// @code
/// if (auto issues = io::saveFile(doc, "bracket.sb"); issues.empty()) {
///     auto opened = io::openFile("bracket.sb");  // same ids, same steps
/// }
/// @endcode
/// @param doc The design to save. On success, doc.isModified() becomes false.
/// @param path Where to save. An existing file is replaced.
/// @return No diagnostics on success; otherwise why the file could not be saved.
Diagnostics saveFile(model::Document& doc, const std::filesystem::path& path);

/// Saves a design as a .sb file without marking it saved, for callers that
/// only have read access to the document (the desktop app saves through the
/// DocumentList, then calls its markSaved()). Same safe write as saveFile().
/// @param doc The design to save.
/// @param path Where to save. An existing file is replaced.
/// @return No diagnostics on success; otherwise why the file could not be saved.
Diagnostics writeFile(const model::Document& doc, const std::filesystem::path& path);

/// Opens a .sb file.
///
/// The opened design has no undo history and is not marked as modified.
/// @param path The file to open.
/// @return The design, or diagnostics saying why it could not be opened.
OpenResult openFile(const std::filesystem::path& path);

/// Writes a design in the .sb format to memory, for example for a browser
/// download or a test. Does not mark the document as saved.
/// @param doc The design to write.
/// @param out Receives the file's bytes.
/// @return No diagnostics on success; otherwise what went wrong.
Diagnostics writeBytes(const model::Document& doc, Bytes& out);

/// Reads a design from the bytes of a .sb file.
/// @param data The file's bytes.
/// @param displayName The file name to use in messages, e.g. "bracket.sb".
/// @return The design, or diagnostics saying why it could not be read.
OpenResult readBytes(const Bytes& data, std::string_view displayName);

}  // namespace sb::io
