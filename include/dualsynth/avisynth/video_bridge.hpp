#pragma once

#include <avisynth.h>

#include <dualsynth/format.hpp>
#include <dualsynth/frame.hpp>
#include <dualsynth/param.hpp>
#include <dualsynth/video_filter.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ds::avisynth {

enum class MtMode {
  NiceFilter,
  MultiInstance,
  Serialized
};

inline ::MtMode host_mt_mode(MtMode mode) {
  switch (mode) {
  case MtMode::NiceFilter:
    return MT_NICE_FILTER;
  case MtMode::MultiInstance:
    return MT_MULTI_INSTANCE;
  case MtMode::Serialized:
    return MT_SERIALIZED;
  }
  return MT_SERIALIZED;
}

inline void set_filter_mt_mode(
  IScriptEnvironment2* env,
  const char* filter_name,
  MtMode mode,
  bool force = false
) {
  env->SetFilterMTMode(filter_name, host_mt_mode(mode), force);
}

inline void set_filter_mt_mode(
  IScriptEnvironment* env,
  const char* filter_name,
  MtMode mode,
  bool force = false
) {
  // AviSynth+ keeps MT registration on IScriptEnvironment2 while plugin init
  // still receives the ABI-stable base interface.
  set_filter_mt_mode(static_cast<IScriptEnvironment2*>(env), filter_name, mode, force);
}

template <class Bridge>
constexpr MtMode bridge_mt_mode() {
  if constexpr (requires { Bridge::avs_mt_mode; }) {
    return Bridge::avs_mt_mode;
  } else {
    return MtMode::NiceFilter;
  }
}

template <class Bridge>
inline void set_video_filter_mt_mode(IScriptEnvironment* env, bool force = false) {
  set_filter_mt_mode(env, Bridge::avs_name, bridge_mt_mode<Bridge>(), force);
}

inline int cache_hint_response(int cachehints, int frame_range, MtMode mt_mode) {
  (void)frame_range;
  if (cachehints == CACHE_GET_MTMODE) {
    return host_mt_mode(mt_mode);
  }
  return 0;
}

template <class Bridge>
inline int cache_hint_response(int cachehints, int frame_range) {
  return cache_hint_response(cachehints, frame_range, bridge_mt_mode<Bridge>());
}

inline int plane_id(VideoFormat format, int plane) {
  if (format.color_family == ColorFamily::Rgb) {
    static constexpr std::array<int, 4> rgb_planes{PLANAR_R, PLANAR_G, PLANAR_B, PLANAR_A};
    return rgb_planes[static_cast<std::size_t>(plane)];
  }

  static constexpr std::array<int, 4> yuv_planes{PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A};
  return yuv_planes[static_cast<std::size_t>(plane)];
}

inline int yuv_pixel_type(VideoFormat format) {
  if (format.plane_count != 3 && format.plane_count != 4) {
    return VideoInfo::CS_UNKNOWN;
  }

  const bool has_alpha = format.plane_count == 4;

  if (format.subsampling_w == 0 && format.subsampling_h == 0) {
    switch (format.sample_format) {
    case SampleFormat::UInt8:
      return has_alpha ? VideoInfo::CS_YUVA444 : VideoInfo::CS_YV24;
    case SampleFormat::UInt10:
      return has_alpha ? VideoInfo::CS_YUVA444P10 : VideoInfo::CS_YUV444P10;
    case SampleFormat::UInt12:
      return has_alpha ? VideoInfo::CS_YUVA444P12 : VideoInfo::CS_YUV444P12;
    case SampleFormat::UInt14:
      return has_alpha ? VideoInfo::CS_YUVA444P14 : VideoInfo::CS_YUV444P14;
    case SampleFormat::UInt16:
      return has_alpha ? VideoInfo::CS_YUVA444P16 : VideoInfo::CS_YUV444P16;
    case SampleFormat::Float32:
      return has_alpha ? VideoInfo::CS_YUVA444PS : VideoInfo::CS_YUV444PS;
    }
  }

  if (format.subsampling_w == 1 && format.subsampling_h == 0) {
    switch (format.sample_format) {
    case SampleFormat::UInt8:
      return has_alpha ? VideoInfo::CS_YUVA422 : VideoInfo::CS_YV16;
    case SampleFormat::UInt10:
      return has_alpha ? VideoInfo::CS_YUVA422P10 : VideoInfo::CS_YUV422P10;
    case SampleFormat::UInt12:
      return has_alpha ? VideoInfo::CS_YUVA422P12 : VideoInfo::CS_YUV422P12;
    case SampleFormat::UInt14:
      return has_alpha ? VideoInfo::CS_YUVA422P14 : VideoInfo::CS_YUV422P14;
    case SampleFormat::UInt16:
      return has_alpha ? VideoInfo::CS_YUVA422P16 : VideoInfo::CS_YUV422P16;
    case SampleFormat::Float32:
      return has_alpha ? VideoInfo::CS_YUVA422PS : VideoInfo::CS_YUV422PS;
    }
  }

  if (format.subsampling_w == 1 && format.subsampling_h == 1) {
    switch (format.sample_format) {
    case SampleFormat::UInt8:
      return has_alpha ? VideoInfo::CS_YUVA420 : VideoInfo::CS_YV12;
    case SampleFormat::UInt10:
      return has_alpha ? VideoInfo::CS_YUVA420P10 : VideoInfo::CS_YUV420P10;
    case SampleFormat::UInt12:
      return has_alpha ? VideoInfo::CS_YUVA420P12 : VideoInfo::CS_YUV420P12;
    case SampleFormat::UInt14:
      return has_alpha ? VideoInfo::CS_YUVA420P14 : VideoInfo::CS_YUV420P14;
    case SampleFormat::UInt16:
      return has_alpha ? VideoInfo::CS_YUVA420P16 : VideoInfo::CS_YUV420P16;
    case SampleFormat::Float32:
      return has_alpha ? VideoInfo::CS_YUVA420PS : VideoInfo::CS_YUV420PS;
    }
  }

  return VideoInfo::CS_UNKNOWN;
}

inline int pixel_type(VideoFormat format) {
  if (format.color_family == ColorFamily::Gray) {
    switch (format.sample_format) {
    case SampleFormat::UInt8:
      return VideoInfo::CS_Y8;
    case SampleFormat::UInt10:
      return VideoInfo::CS_Y10;
    case SampleFormat::UInt12:
      return VideoInfo::CS_Y12;
    case SampleFormat::UInt14:
      return VideoInfo::CS_Y14;
    case SampleFormat::UInt16:
      return VideoInfo::CS_Y16;
    case SampleFormat::Float32:
      return VideoInfo::CS_Y32;
    }
  }

  if (format.color_family == ColorFamily::Rgb && format.plane_count == 3) {
    switch (format.sample_format) {
    case SampleFormat::UInt8:
      return VideoInfo::CS_RGBP;
    case SampleFormat::UInt10:
      return VideoInfo::CS_RGBP10;
    case SampleFormat::UInt12:
      return VideoInfo::CS_RGBP12;
    case SampleFormat::UInt14:
      return VideoInfo::CS_RGBP14;
    case SampleFormat::UInt16:
      return VideoInfo::CS_RGBP16;
    case SampleFormat::Float32:
      return VideoInfo::CS_RGBPS;
    }
  }

  if (format.color_family == ColorFamily::Rgb && format.plane_count == 4) {
    switch (format.sample_format) {
    case SampleFormat::UInt8:
      return VideoInfo::CS_RGBAP;
    case SampleFormat::UInt10:
      return VideoInfo::CS_RGBAP10;
    case SampleFormat::UInt12:
      return VideoInfo::CS_RGBAP12;
    case SampleFormat::UInt14:
      return VideoInfo::CS_RGBAP14;
    case SampleFormat::UInt16:
      return VideoInfo::CS_RGBAP16;
    case SampleFormat::Float32:
      return VideoInfo::CS_RGBAPS;
    }
  }

  if (format.color_family == ColorFamily::Yuv) {
    return yuv_pixel_type(format);
  }

  return VideoInfo::CS_UNKNOWN;
}

inline Result<SampleFormat> sample_format_from_pixel_type(int pixel_type) {
  switch (pixel_type & VideoInfo::CS_Sample_Bits_Mask) {
  case VideoInfo::CS_Sample_Bits_8:
    return Result<SampleFormat>::success(SampleFormat::UInt8);
  case VideoInfo::CS_Sample_Bits_10:
    return Result<SampleFormat>::success(SampleFormat::UInt10);
  case VideoInfo::CS_Sample_Bits_12:
    return Result<SampleFormat>::success(SampleFormat::UInt12);
  case VideoInfo::CS_Sample_Bits_14:
    return Result<SampleFormat>::success(SampleFormat::UInt14);
  case VideoInfo::CS_Sample_Bits_16:
    return Result<SampleFormat>::success(SampleFormat::UInt16);
  case VideoInfo::CS_Sample_Bits_32:
    return Result<SampleFormat>::success(SampleFormat::Float32);
  default:
    return Result<SampleFormat>::failure({
      ErrorCode::UnsupportedFormat,
      "unsupported AviSynth sample depth"
    });
  }
}

inline int subsampling_from_pixel_type(int pixel_type, int mask, int sub_1, int sub_2, int sub_4) {
  const int value = pixel_type & mask;
  if (value == sub_1) {
    return 0;
  }
  if (value == sub_2) {
    return 1;
  }
  if (value == sub_4) {
    return 2;
  }
  return 0;
}

inline Result<VideoFormat> video_format_from_pixel_type(int pixel_type) {
  if (pixel_type == VideoInfo::CS_UNKNOWN) {
    return Result<VideoFormat>::failure({
      ErrorCode::UnsupportedFormat,
      "unknown AviSynth pixel type"
    });
  }

  auto sample_format = sample_format_from_pixel_type(pixel_type);
  if (!sample_format.has_value()) {
    return Result<VideoFormat>::failure(sample_format.error());
  }

  if (
    (pixel_type & VideoInfo::CS_PLANAR) &&
    (pixel_type & VideoInfo::CS_BGR) &&
    (pixel_type & (VideoInfo::CS_RGB_TYPE | VideoInfo::CS_RGBA_TYPE))
  ) {
    const int plane_count = (pixel_type & VideoInfo::CS_RGBA_TYPE) ? 4 : 3;
    return Result<VideoFormat>::success(
      VideoFormat{ColorFamily::Rgb, sample_format.value(), plane_count, 0, 0}
    );
  }

  if (pixel_type & VideoInfo::CS_YUVA) {
    const int subsampling_w = subsampling_from_pixel_type(
      pixel_type,
      VideoInfo::CS_Sub_Width_Mask,
      VideoInfo::CS_Sub_Width_1,
      VideoInfo::CS_Sub_Width_2,
      VideoInfo::CS_Sub_Width_4
    );
    const int subsampling_h = subsampling_from_pixel_type(
      pixel_type,
      VideoInfo::CS_Sub_Height_Mask,
      VideoInfo::CS_Sub_Height_1,
      VideoInfo::CS_Sub_Height_2,
      VideoInfo::CS_Sub_Height_4
    );
    return Result<VideoFormat>::success(
      VideoFormat{ColorFamily::Yuv, sample_format.value(), 4, subsampling_w, subsampling_h}
    );
  }

  if (
    (pixel_type & VideoInfo::CS_PLANAR) &&
    (pixel_type & VideoInfo::CS_YUV) &&
    (pixel_type & VideoInfo::CS_INTERLEAVED)
  ) {
    return Result<VideoFormat>::success(
      VideoFormat{ColorFamily::Gray, sample_format.value(), 1, 0, 0}
    );
  }

  if (
    (pixel_type & VideoInfo::CS_PLANAR) &&
    (pixel_type & VideoInfo::CS_YUV) &&
    !(pixel_type & VideoInfo::CS_INTERLEAVED)
  ) {
    const int subsampling_w = subsampling_from_pixel_type(
      pixel_type,
      VideoInfo::CS_Sub_Width_Mask,
      VideoInfo::CS_Sub_Width_1,
      VideoInfo::CS_Sub_Width_2,
      VideoInfo::CS_Sub_Width_4
    );
    const int subsampling_h = subsampling_from_pixel_type(
      pixel_type,
      VideoInfo::CS_Sub_Height_Mask,
      VideoInfo::CS_Sub_Height_1,
      VideoInfo::CS_Sub_Height_2,
      VideoInfo::CS_Sub_Height_4
    );
    return Result<VideoFormat>::success(
      VideoFormat{ColorFamily::Yuv, sample_format.value(), 3, subsampling_w, subsampling_h}
    );
  }

  return Result<VideoFormat>::failure({
    ErrorCode::UnsupportedFormat,
    "unsupported AviSynth pixel type"
  });
}

inline Result<VideoFormat> make_video_format(const VideoInfo& vi) {
  return video_format_from_pixel_type(vi.pixel_type);
}

inline void initialize_no_audio(VideoInfo& vi) {
  vi.audio_samples_per_second = 0;
  vi.sample_type = 0;
  vi.num_audio_samples = 0;
  vi.nchannels = 0;
}

inline void initialize_no_video(VideoInfo& vi) {
  vi.width = 0;
  vi.height = 0;
  vi.fps_numerator = 0;
  vi.fps_denominator = 1;
  vi.num_frames = 0;
  vi.pixel_type = VideoInfo::CS_UNKNOWN;
}

inline VideoInfo make_video_info(const VideoOutputInfo& output) {
  VideoInfo vi{};
  vi.width = output.width;
  vi.height = output.height;
  vi.fps_numerator = static_cast<unsigned>(output.fps.numerator);
  vi.fps_denominator = static_cast<unsigned>(output.fps.denominator);
  vi.num_frames = output.num_frames;
  vi.pixel_type = pixel_type(output.format);
  vi.image_type = 0;
  initialize_no_audio(vi);
  return vi;
}

inline VideoFrameView make_video_frame_view(
  const PVideoFrame& frame,
  VideoFormat format
) {
  std::array<PlaneView, 4> planes{};
  const int sample_bytes = bytes_per_sample(format.sample_format);
  for (int plane = 0; plane < format.plane_count; ++plane) {
    const int host_plane = plane_id(format, plane);
    planes[static_cast<std::size_t>(plane)] = PlaneView{
      frame->GetReadPtr(host_plane),
      frame->GetPitch(host_plane),
      frame->GetRowSize(host_plane) / sample_bytes,
      frame->GetHeight(host_plane)
    };
  }
  return VideoFrameView{format, format.plane_count, planes};
}

inline MutableVideoFrameView make_mutable_video_frame_view(
  const PVideoFrame& frame,
  VideoFormat format
) {
  std::array<MutablePlaneView, 4> planes{};
  const int sample_bytes = bytes_per_sample(format.sample_format);
  for (int plane = 0; plane < format.plane_count; ++plane) {
    const int host_plane = plane_id(format, plane);
    planes[static_cast<std::size_t>(plane)] = MutablePlaneView{
      frame->GetWritePtr(host_plane),
      frame->GetPitch(host_plane),
      frame->GetRowSize(host_plane) / sample_bytes,
      frame->GetHeight(host_plane)
    };
  }
  return MutableVideoFrameView{format, format.plane_count, planes};
}

template <class Source>
Result<ParamValues> read_params_from_source(
  const Source& source,
  const FilterDescriptor& descriptor
) {
  auto validation = validate_filter_descriptor(descriptor);
  if (!validation.has_value()) {
    return Result<ParamValues>::failure(validation.error());
  }

  int base_count = 0;
  for (const auto& param : descriptor.params) {
    if (param.avs_enabled) {
      ++base_count;
    }
  }

  ParamValues values{};
  int base_index = 0;
  int array_index = base_count;

  for (const auto& param : descriptor.params) {
    if (!param.avs_enabled) {
      continue;
    }

    const int current_base_index = base_index++;
    if (param.type == ParamType::Clip) {
      continue;
    }

    try {
      if (param.is_array) {
        const int current_array_index = array_index++;
        if (source.defined(current_array_index)) {
          switch (param.type) {
          case ParamType::Integer:
            values.entries.push_back(ParamEntry{
              param.name,
              ParamValue{source.as_int_array(current_array_index)}
            });
            break;
          case ParamType::Float:
            values.entries.push_back(ParamEntry{
              param.name,
              ParamValue{source.as_float_array(current_array_index)}
            });
            break;
          case ParamType::Boolean:
            values.entries.push_back(ParamEntry{
              param.name,
              ParamValue{source.as_bool_array(current_array_index)}
            });
            break;
          case ParamType::String:
            values.entries.push_back(ParamEntry{
              param.name,
              ParamValue{source.as_string_array(current_array_index)}
            });
            break;
          case ParamType::Clip:
            break;
          }
          continue;
        }

        if (source.defined(current_base_index)) {
          values.entries.push_back(ParamEntry{
            param.name,
            ParamValue{source.as_string(current_base_index)}
          });
          continue;
        }

        if (param.required) {
          return Result<ParamValues>::failure({
            ErrorCode::InvalidArgument,
            "missing required AviSynth array parameter '" + param.name + "'"
          });
        }
        continue;
      }

      if (!source.defined(current_base_index)) {
        if (param.required) {
          return Result<ParamValues>::failure({
            ErrorCode::InvalidArgument,
            "missing required AviSynth parameter '" + param.name + "'"
          });
        }
        continue;
      }

      switch (param.type) {
      case ParamType::Integer:
        values.entries.push_back(ParamEntry{
          param.name,
          ParamValue{source.as_int(current_base_index)}
        });
        break;
      case ParamType::Float:
        values.entries.push_back(ParamEntry{
          param.name,
          ParamValue{source.as_float(current_base_index)}
        });
        break;
      case ParamType::Boolean:
        values.entries.push_back(ParamEntry{
          param.name,
          ParamValue{source.as_bool(current_base_index)}
        });
        break;
      case ParamType::String:
        values.entries.push_back(ParamEntry{
          param.name,
          ParamValue{source.as_string(current_base_index)}
        });
        break;
      case ParamType::Clip:
        break;
      }
    } catch (...) {
      return Result<ParamValues>::failure({
        ErrorCode::InvalidArgument,
        "AviSynth parameter '" + param.name + "' has the wrong type"
      });
    }
  }

  return Result<ParamValues>::success(std::move(values));
}

class AvisynthValueParamSource {
public:
  explicit AvisynthValueParamSource(const AVSValue& args) : args_(args) {}

  bool defined(int index) const {
    return args_[index].Defined();
  }

  std::int64_t as_int(int index) const {
    return args_[index].AsInt();
  }

  double as_float(int index) const {
    return args_[index].AsFloat();
  }

  bool as_bool(int index) const {
    return args_[index].AsBool();
  }

  std::string as_string(int index) const {
    return args_[index].AsString();
  }

  std::vector<std::int64_t> as_int_array(int index) const {
    const AVSValue& array = args_[index];
    std::vector<std::int64_t> output;
    output.reserve(static_cast<std::size_t>(array.ArraySize()));
    for (int i = 0; i < array.ArraySize(); ++i) {
      output.push_back(array[i].AsInt());
    }
    return output;
  }

  std::vector<double> as_float_array(int index) const {
    const AVSValue& array = args_[index];
    std::vector<double> output;
    output.reserve(static_cast<std::size_t>(array.ArraySize()));
    for (int i = 0; i < array.ArraySize(); ++i) {
      output.push_back(array[i].AsFloat());
    }
    return output;
  }

  std::vector<bool> as_bool_array(int index) const {
    const AVSValue& array = args_[index];
    std::vector<bool> output;
    output.reserve(static_cast<std::size_t>(array.ArraySize()));
    for (int i = 0; i < array.ArraySize(); ++i) {
      output.push_back(array[i].AsBool());
    }
    return output;
  }

  std::vector<std::string> as_string_array(int index) const {
    const AVSValue& array = args_[index];
    std::vector<std::string> output;
    output.reserve(static_cast<std::size_t>(array.ArraySize()));
    for (int i = 0; i < array.ArraySize(); ++i) {
      output.emplace_back(array[i].AsString());
    }
    return output;
  }

private:
  const AVSValue& args_;
};

inline Result<ParamValues> read_params(
  const AVSValue& args,
  const FilterDescriptor& descriptor
) {
  return read_params_from_source(AvisynthValueParamSource{args}, descriptor);
}

} // namespace ds::avisynth
