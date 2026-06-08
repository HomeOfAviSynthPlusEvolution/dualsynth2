#pragma once

#include <vapoursynth/VapourSynth4.h>

#include <dualsynth/format.hpp>
#include <dualsynth/frame.hpp>
#include <dualsynth/param.hpp>
#include <dualsynth/video_bridge.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <utility>

namespace ds::vapoursynth {

inline int color_family(VideoFormat format) {
  switch (format.color_family) {
  case ColorFamily::Gray:
    return cfGray;
  case ColorFamily::Rgb:
    return cfRGB;
  case ColorFamily::Yuv:
    return format.plane_count == 1 ? cfGray : cfYUV;
  }
  return cfUndefined;
}

inline int sample_type(SampleFormat sample_format) {
  return sample_format == SampleFormat::Float32 ? stFloat : stInteger;
}

inline bool query_video_format(
  VideoFormat format,
  VSVideoFormat& output,
  VSCore* core,
  const VSAPI* vsapi
) {
  return vsapi->queryVideoFormat(
    &output,
    color_family(format),
    sample_type(format.sample_format),
    bits_per_sample(format.sample_format),
    format.subsampling_w,
    format.subsampling_h,
    core
  ) != 0;
}

inline VideoFrameView make_video_frame_view(
  const VSFrame* frame,
  VideoFormat format,
  const VSAPI* vsapi
) {
  std::array<PlaneView, 4> planes{};
  for (int plane = 0; plane < format.plane_count; ++plane) {
    planes[static_cast<std::size_t>(plane)] = PlaneView{
      vsapi->getReadPtr(frame, plane),
      vsapi->getStride(frame, plane),
      vsapi->getFrameWidth(frame, plane),
      vsapi->getFrameHeight(frame, plane)
    };
  }
  return VideoFrameView{format, format.plane_count, planes};
}

inline MutableVideoFrameView make_mutable_video_frame_view(
  VSFrame* frame,
  VideoFormat format,
  const VSAPI* vsapi
) {
  std::array<MutablePlaneView, 4> planes{};
  for (int plane = 0; plane < format.plane_count; ++plane) {
    planes[static_cast<std::size_t>(plane)] = MutablePlaneView{
      vsapi->getWritePtr(frame, plane),
      vsapi->getStride(frame, plane),
      vsapi->getFrameWidth(frame, plane),
      vsapi->getFrameHeight(frame, plane)
    };
  }
  return MutableVideoFrameView{format, format.plane_count, planes};
}

inline ParamValues read_optional_int_params(
  const VSMap* in,
  std::span<const char* const> names,
  const VSAPI* vsapi
) {
  ParamValues values{};
  for (const char* name : names) {
    int error = 0;
    const int value = vsapi->mapGetIntSaturated(in, name, 0, &error);
    if (error == peSuccess) {
      values.entries.push_back(ParamEntry{name, ParamValue{value}});
    }
  }
  return values;
}

template <VideoBridge Bridge, class Creator>
decltype(auto) create_video_filter_bridge(Creator&& creator) {
  return std::forward<Creator>(creator).template operator()<typename Bridge::Core>(
    Bridge::vs_input_names,
    Bridge::missing_input_error,
    Bridge::vs_format_error
  );
}

} // namespace ds::vapoursynth
