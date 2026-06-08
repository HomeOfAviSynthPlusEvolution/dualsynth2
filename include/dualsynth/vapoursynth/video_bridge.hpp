#pragma once

#include <vapoursynth/VapourSynth4.h>

#include <dualsynth/format.hpp>
#include <dualsynth/frame.hpp>
#include <dualsynth/param.hpp>
#include <dualsynth/video_bridge.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

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

inline Result<ParamValues> read_params(
  const VSMap* in,
  const FilterDescriptor& descriptor,
  const VSAPI* vsapi
) {
  auto validation = validate_filter_descriptor(descriptor);
  if (!validation.has_value()) {
    return Result<ParamValues>::failure(validation.error());
  }

  ParamValues values{};
  for (const auto& param : descriptor.params) {
    if (!param.vs_enabled || param.type == ParamType::Clip) {
      continue;
    }

    const int element_count = vsapi->mapNumElements(in, param.name.c_str());
    if (element_count < 0) {
      if (param.required) {
        return Result<ParamValues>::failure({
          ErrorCode::InvalidArgument,
          "missing required VapourSynth parameter '" + param.name + "'"
        });
      }
      continue;
    }

    if (param.is_array) {
      switch (param.type) {
      case ParamType::Integer: {
        std::vector<std::int64_t> output;
        output.reserve(static_cast<std::size_t>(element_count));
        for (int i = 0; i < element_count; ++i) {
          int error = 0;
          const std::int64_t value = vsapi->mapGetInt(in, param.name.c_str(), i, &error);
          if (error != peSuccess) {
            return Result<ParamValues>::failure({
              ErrorCode::InvalidArgument,
              "VapourSynth parameter '" + param.name + "' must be an integer array"
            });
          }
          output.push_back(value);
        }
        values.entries.push_back(ParamEntry{param.name, ParamValue{std::move(output)}});
        break;
      }
      case ParamType::Float: {
        std::vector<double> output;
        output.reserve(static_cast<std::size_t>(element_count));
        for (int i = 0; i < element_count; ++i) {
          int error = 0;
          const double value = vsapi->mapGetFloat(in, param.name.c_str(), i, &error);
          if (error != peSuccess) {
            return Result<ParamValues>::failure({
              ErrorCode::InvalidArgument,
              "VapourSynth parameter '" + param.name + "' must be a float array"
            });
          }
          output.push_back(value);
        }
        values.entries.push_back(ParamEntry{param.name, ParamValue{std::move(output)}});
        break;
      }
      case ParamType::Boolean: {
        std::vector<bool> output;
        output.reserve(static_cast<std::size_t>(element_count));
        for (int i = 0; i < element_count; ++i) {
          int error = 0;
          const std::int64_t value = vsapi->mapGetInt(in, param.name.c_str(), i, &error);
          if (error != peSuccess) {
            return Result<ParamValues>::failure({
              ErrorCode::InvalidArgument,
              "VapourSynth parameter '" + param.name + "' must be a boolean array"
            });
          }
          output.push_back(value != 0);
        }
        values.entries.push_back(ParamEntry{param.name, ParamValue{std::move(output)}});
        break;
      }
      case ParamType::String: {
        std::vector<std::string> output;
        output.reserve(static_cast<std::size_t>(element_count));
        for (int i = 0; i < element_count; ++i) {
          int error = 0;
          const char* value = vsapi->mapGetData(in, param.name.c_str(), i, &error);
          if (error != peSuccess) {
            return Result<ParamValues>::failure({
              ErrorCode::InvalidArgument,
              "VapourSynth parameter '" + param.name + "' must be a data array"
            });
          }
          output.emplace_back(value);
        }
        values.entries.push_back(ParamEntry{param.name, ParamValue{std::move(output)}});
        break;
      }
      case ParamType::Clip:
        break;
      }
      continue;
    }

    int error = 0;
    switch (param.type) {
    case ParamType::Integer:
      values.entries.push_back(ParamEntry{
        param.name,
        ParamValue{vsapi->mapGetInt(in, param.name.c_str(), 0, &error)}
      });
      break;
    case ParamType::Float:
      values.entries.push_back(ParamEntry{
        param.name,
        ParamValue{vsapi->mapGetFloat(in, param.name.c_str(), 0, &error)}
      });
      break;
    case ParamType::Boolean:
      values.entries.push_back(ParamEntry{
        param.name,
        ParamValue{vsapi->mapGetInt(in, param.name.c_str(), 0, &error) != 0}
      });
      break;
    case ParamType::String:
      if (const char* value = vsapi->mapGetData(in, param.name.c_str(), 0, &error);
          error == peSuccess) {
        values.entries.push_back(ParamEntry{
          param.name,
          ParamValue{std::string(value ? value : "")}
        });
      }
      break;
    case ParamType::Clip:
      break;
    }

    if (error != peSuccess) {
      return Result<ParamValues>::failure({
        ErrorCode::InvalidArgument,
        "VapourSynth parameter '" + param.name + "' has the wrong type"
      });
    }
  }

  return Result<ParamValues>::success(std::move(values));
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
