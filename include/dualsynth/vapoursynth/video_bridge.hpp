#pragma once

#include <dualsynth/video_bridge.hpp>

#include <utility>

namespace ds::vapoursynth {

template <VideoBridge Bridge, class Creator>
decltype(auto) create_video_filter_bridge(Creator&& creator) {
  return std::forward<Creator>(creator).template operator()<typename Bridge::Core>(
    Bridge::vs_input_names,
    Bridge::missing_input_error,
    Bridge::vs_format_error
  );
}

} // namespace ds::vapoursynth
