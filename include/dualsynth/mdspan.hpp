#pragma once

#include <array>
#include <cstddef>
#include <mdspan>
#include <type_traits>

#if !defined(__cpp_lib_mdspan)
#error "DualSynth requires a C++23 standard library with std::mdspan support."
#endif

namespace ds {

template <class T>
using PlaneView2D = std::mdspan<
  T,
  std::dextents<std::size_t, 2>,
  std::layout_stride
>;

template <class T>
constexpr PlaneView2D<T> make_plane_view(
  T* data,
  int width,
  int height,
  std::ptrdiff_t stride_bytes
) {
  using Sample = std::remove_const_t<T>;
  using Extents = std::dextents<std::size_t, 2>;
  using Mapping = std::layout_stride::mapping<Extents>;

  return PlaneView2D<T>{
    data,
    Mapping{
      Extents{
        static_cast<std::size_t>(height),
        static_cast<std::size_t>(width)
      },
      std::array<std::size_t, 2>{
        static_cast<std::size_t>(stride_bytes) / sizeof(Sample),
        1
      }
    }
  };
}

} // namespace ds
