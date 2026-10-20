#pragma once

#include <string>
#include <vector>

namespace kh {

enum class Severity { Info, Warning, Error };

// A problem reported to the user. `message` is written for a student:
// plain words, and it says how to fix the problem when possible.
// Errors cross library boundaries as Diagnostics, never as exceptions.
struct Diagnostic {
    std::string code;     // stable machine-readable code, e.g. "kernel.boolean.failed"
    Severity severity = Severity::Error;
    std::string message;  // plain-language explanation
    std::string detail;   // technical detail, shown behind a "Details" link
};

using Diagnostics = std::vector<Diagnostic>;

}  // namespace kh
