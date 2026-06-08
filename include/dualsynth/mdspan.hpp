#pragma once

#include <cstddef>
#include <type_traits>

namespace ds {

template <class T>
class PlaneView2D {
public:
  using element_type = T;
  using index_type = std::size_t;

  constexpr PlaneView2D() = default;

  constexpr PlaneView2D(
    T* data,
    index_type height,
    index_type width,
    index_type stride_elements
  ) noexcept
    : data_(data),
      height_(height),
      width_(width),
      stride_elements_(stride_elements) {}

  constexpr index_type extent(index_type dimension) const noexcept {
    return dimension == 0 ? height_ : width_;
  }

  constexpr T& operator()(index_type y, index_type x) const noexcept {
    return data_[y * stride_elements_ + x];
  }

private:
  T* data_ = nullptr;
  index_type height_ = 0;
  index_type width_ = 0;
  index_type stride_elements_ = 0;
};

template <class T>
constexpr PlaneView2D<T> make_plane_view(
  T* data,
  int width,
  int height,
  std::ptrdiff_t stride_bytes
) noexcept {
  using Sample = std::remove_const_t<T>;
  return PlaneView2D<T>(
    data,
    static_cast<std::size_t>(height),
    static_cast<std::size_t>(width),
    static_cast<std::size_t>(stride_bytes) / sizeof(Sample)
  );
}

} // namespace ds
