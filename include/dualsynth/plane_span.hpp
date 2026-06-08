#pragma once

#include <cstddef>
#include <span>
#include <type_traits>

namespace ds {

template <class T>
class PlaneSpan {
public:
  PlaneSpan(T* data, int width, int height, std::ptrdiff_t stride_bytes) noexcept
    : data_(data),
      width_(width),
      height_(height),
      stride_bytes_(stride_bytes) {}

  int width() const noexcept {
    return width_;
  }

  int height() const noexcept {
    return height_;
  }

  std::ptrdiff_t stride_bytes() const noexcept {
    return stride_bytes_;
  }

  T& operator()(int y, int x) const noexcept {
    return row(y)[static_cast<std::size_t>(x)];
  }

  std::span<T> row(int y) const noexcept {
    using Byte = std::conditional_t<std::is_const_v<T>, const std::byte, std::byte>;
    auto* bytes = reinterpret_cast<Byte*>(data_);
    auto* row_bytes = bytes + static_cast<std::ptrdiff_t>(y) * stride_bytes_;
    return {
      reinterpret_cast<T*>(row_bytes),
      static_cast<std::size_t>(width_)
    };
  }

private:
  T* data_;
  int width_;
  int height_;
  std::ptrdiff_t stride_bytes_;
};

} // namespace ds
