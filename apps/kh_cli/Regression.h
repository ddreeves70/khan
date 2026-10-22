#pragma once

#include <filesystem>
#include <vector>

#include "kh/kernel/IKernel.h"

namespace kh::cli {

struct RegressionOptions {
    bool update = false;  // rewrite each case's "expect" block with the actual results
};

// Runs regression cases (JSON files) against the kernel and prints a PASS/FAIL
// line per case. `paths` may contain case files and folders of case files.
// Returns the process exit code: 0 if every case passed, 1 otherwise.
int runRegression(kernel::IKernel& kernel, const std::vector<std::filesystem::path>& paths,
                  const RegressionOptions& options);

}  // namespace kh::cli
