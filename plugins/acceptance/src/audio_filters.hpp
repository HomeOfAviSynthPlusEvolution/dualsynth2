#pragma once

#include <dualsynth/span2d.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace ds::reference {

template <class T>
void copy_samples(Span<const T> src, Span<T> dst) {
  std::copy(src.begin(), src.end(), dst.begin());
}

template <class T>
T scale_sample(T sample, double gain) {
  const double scaled = static_cast<double>(sample) * gain;
  if constexpr (std::is_integral_v<T>) {
    const double lo = static_cast<double>(std::numeric_limits<T>::min());
    const double hi = static_cast<double>(std::numeric_limits<T>::max());
    return static_cast<T>(std::clamp(std::nearbyint(scaled), lo, hi));
  } else {
    return static_cast<T>(scaled);
  }
}

template <class T>
void gain_samples(Span<const T> src, Span<T> dst, double gain) {
  for (std::size_t i = 0; i < src.size(); ++i) {
    dst[i] = scale_sample(src[i], gain);
  }
}

} // namespace ds::reference
