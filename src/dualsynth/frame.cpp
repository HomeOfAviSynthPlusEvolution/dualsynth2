#include <dualsynth/frame.hpp>

#include <utility>

namespace ds {

FrameHandle::FrameHandle(void* user, FrameReleaseFn release) noexcept
  : user_(user), release_(release) {}

FrameHandle::~FrameHandle() {
  reset();
}

FrameHandle::FrameHandle(FrameHandle&& other) noexcept
  : user_(std::exchange(other.user_, nullptr)),
    release_(std::exchange(other.release_, nullptr)) {}

FrameHandle& FrameHandle::operator=(FrameHandle&& other) noexcept {
  if (this != &other) {
    reset();
    user_ = std::exchange(other.user_, nullptr);
    release_ = std::exchange(other.release_, nullptr);
  }
  return *this;
}

bool FrameHandle::valid() const noexcept {
  return user_ != nullptr;
}

void* FrameHandle::user() const noexcept {
  return user_;
}

void FrameHandle::reset() noexcept {
  if (user_ != nullptr && release_ != nullptr) {
    release_(user_);
  }
  user_ = nullptr;
  release_ = nullptr;
}

} // namespace ds
