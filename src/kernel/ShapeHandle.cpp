#include <atomic>

#include "kh/kernel/Types.h"

namespace kh::kernel {

namespace {
std::atomic<std::uint64_t> nextShapeId{1};
}

ShapeHandle::ShapeHandle(std::shared_ptr<const ShapeImpl> impl)
    : impl_(std::move(impl)), id_(impl_ ? nextShapeId.fetch_add(1) : 0) {}

}  // namespace kh::kernel
