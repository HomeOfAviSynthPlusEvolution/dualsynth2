#pragma once

#include <array>
#include <cstddef>
#include <type_traits>

#ifndef DS_USE_STD_MDSPAN
#error "DualSynth mdspan backend was not configured. Link plugin targets against DualSynth::dualsynth."
#endif

#if DS_USE_STD_MDSPAN
#include <mdspan>
#else
#include <experimental/mdspan>
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
