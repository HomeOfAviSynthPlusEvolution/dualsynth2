#pragma once

#include <dualsynth/plane_span.hpp>

#include <type_traits>

namespace ds::reference {

template <class T>
void copy_plane(PlaneSpan<const T> src, PlaneSpan<T> dst) {
  for (int y = 0; y < src.height(); ++y) {
    const auto src_row = src.row(y);
    const auto dst_row = dst.row(y);
    for (int x = 0; x < src.width(); ++x) {
      dst_row[static_cast<std::size_t>(x)] = src_row[static_cast<std::size_t>(x)];
    }
  }
}

template <class T>
constexpr T max_sample_value() {
  if constexpr (std::is_same_v<T, unsigned char>) {
    return 255;
  } else if constexpr (std::is_same_v<T, unsigned short>) {
    return 65535;
  } else {
    return T{1};
  }
}

template <class T>
void invert_plane(PlaneSpan<const T> src, PlaneSpan<T> dst) {
  const T max_value = max_sample_value<T>();
  for (int y = 0; y < src.height(); ++y) {
    const auto src_row = src.row(y);
    const auto dst_row = dst.row(y);
    for (int x = 0; x < src.width(); ++x) {
      dst_row[static_cast<std::size_t>(x)] =
        static_cast<T>(max_value - src_row[static_cast<std::size_t>(x)]);
    }
  }
}

} // namespace ds::reference
