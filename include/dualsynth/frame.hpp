#pragma once

#include <dualsynth/format.hpp>
#include <dualsynth/span2d.hpp>

#include <array>
#include <cstddef>
#include <stdexcept>

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

struct VideoFrameView {
  VideoFormat format{ColorFamily::Gray, SampleFormat::UInt8, 1, 0, 0};
  int plane_count = 0;
  std::array<PlaneView, 4> planes{};

  const PlaneView& plane(int index) const {
    if (index < 0 || index >= plane_count) {
      throw std::out_of_range("video plane index is out of range");
    }
    return planes[static_cast<std::size_t>(index)];
  }
};

struct MutableVideoFrameView {
  VideoFormat format{ColorFamily::Gray, SampleFormat::UInt8, 1, 0, 0};
  int plane_count = 0;
  std::array<MutablePlaneView, 4> planes{};

  MutablePlaneView& plane(int index) {
    if (index < 0 || index >= plane_count) {
      throw std::out_of_range("video plane index is out of range");
    }
    return planes[static_cast<std::size_t>(index)];
  }

  const MutablePlaneView& plane(int index) const {
    if (index < 0 || index >= plane_count) {
      throw std::out_of_range("video plane index is out of range");
    }
    return planes[static_cast<std::size_t>(index)];
  }
};

template <class T>
span2d::Plane<const T> as_plane(const PlaneView& plane) {
  return span2d::make_plane(
    static_cast<const T*>(plane.data),
    plane.width,
    plane.height,
    plane.stride_bytes
  );
}

template <class T>
span2d::Plane<T> as_plane(const MutablePlaneView& plane) {
  return span2d::make_plane(
    static_cast<T*>(plane.data),
    plane.width,
    plane.height,
    plane.stride_bytes
  );
}

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
