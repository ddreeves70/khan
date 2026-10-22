#pragma once

/// @file
/// Problems reported to the user, used instead of exceptions.

#include <string>
#include <vector>

namespace kh {

/// How serious a Diagnostic is.
enum class Severity {
    Info,     ///< Something worth knowing; nothing is wrong.
    Warning,  ///< The result was made but may not be what was wanted.
    Error,    ///< The operation failed.
};

/// A problem reported to the user.
///
/// Errors cross library boundaries as Diagnostics, never as exceptions.
/// The message is written for a 14-year-old: plain words, units on every
/// number, and how to fix the problem when possible.
struct Diagnostic {
    /// Stable machine-readable code, e.g. "kernel.boolean.failed". Tests and
    /// tools match on this, so it does not change when the wording does.
    std::string code;
    /// How serious the problem is.
    Severity severity = Severity::Error;
    /// Plain-language explanation shown to the user.
    std::string message;
    /// Technical detail, shown behind a "Details" link. May be empty.
    std::string detail;
};

/// A list of problems. Empty means success.
using Diagnostics = std::vector<Diagnostic>;

}  // namespace kh
