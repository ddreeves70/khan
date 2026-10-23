#pragma once

/// @file
/// Application and file-format versions.

#include <string_view>

namespace kh {

/// Gives the application version.
/// @return The version as text, for example "0.0.1".
std::string_view version();

/// Version of the .khan file schema this build writes. Files with a higher number
/// were saved by a newer Khan and are refused with a message to update.
/// See docs/file-format.md.
inline constexpr int kFileSchemaVersion = 1;

}  // namespace kh
