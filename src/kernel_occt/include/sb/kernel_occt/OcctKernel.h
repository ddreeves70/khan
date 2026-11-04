#pragma once

/// @file
/// Factory for the Open CASCADE kernel.

#include <memory>

#include "sb/kernel/IKernel.h"

namespace sb::kernel {

/// Creates the default kernel, backed by Open CASCADE Technology (OCCT).
///
/// Callers hold it as an IKernel and never see OCCT types.
/// @return A new kernel. Each call makes an independent instance.
std::unique_ptr<IKernel> makeOcctKernel();

}  // namespace sb::kernel
