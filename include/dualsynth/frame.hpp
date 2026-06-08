#pragma once

#include <cstddef>

namespace ds {

using FrameReleaseFn = void (*)(void*);

struct PlaneView {
  const void* data;
  std::ptrdiff_t stride_bytes;
  int width;
  int height;
};

struct MutablePlaneView {
  void* data;
  std::ptrdiff_t stride_bytes;
  int width;
  int height;
};

class FrameHandle {
public:
  FrameHandle() noexcept = default;
  FrameHandle(void* user, FrameReleaseFn release) noexcept;
  ~FrameHandle();

  FrameHandle(const FrameHandle&) = delete;
  FrameHandle& operator=(const FrameHandle&) = delete;

  FrameHandle(FrameHandle&& other) noexcept;
  FrameHandle& operator=(FrameHandle&& other) noexcept;

  bool valid() const noexcept;
  void* user() const noexcept;

private:
  void reset() noexcept;

  void* user_ = nullptr;
  FrameReleaseFn release_ = nullptr;
};

} // namespace ds
