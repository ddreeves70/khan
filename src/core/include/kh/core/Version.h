#pragma once

#include <string_view>

namespace kh {

// Application version, for example "0.0.1".
std::string_view version();

// Version of the .khan file schema this build writes.
inline constexpr int kFileSchemaVersion = 0;

}  // namespace kh
