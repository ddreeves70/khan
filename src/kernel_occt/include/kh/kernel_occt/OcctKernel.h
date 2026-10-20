#pragma once

#include <memory>

#include "kh/kernel/IKernel.h"

namespace kh::kernel {

// Creates the default kernel, backed by Open CASCADE Technology.
// Callers hold it as an IKernel and never see OCCT types.
std::unique_ptr<IKernel> makeOcctKernel();

}  // namespace kh::kernel
