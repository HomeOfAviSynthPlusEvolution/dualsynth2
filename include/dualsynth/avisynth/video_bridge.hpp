#pragma once

#include <avisynth.h>

#include <dualsynth/avisynth/global_lock.hpp>
#include <dualsynth/format.hpp>
#include <dualsynth/frame.hpp>
#include <dualsynth/param.hpp>
#include <dualsynth/video_bridge.hpp>
#include <dualsynth/video_filter.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ds::avisynth {

template <class Filter>
constexpr OutputOrigin filter_output_origin() {
  if constexpr (requires { Filter::output_origin; }) {
    return Filter::output_origin;
  } else {
    return OutputOrigin::fresh();
  }
}

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

inline bool output_origin_matches(
  OutputOrigin origin,
  const VideoOutputInfo& output,
  std::span<const VideoInputInfo> inputs
) {
  if (origin.kind == OutputOriginKind::Fresh) {
    return true;
  }
  if (origin.input_index < 0 || static_cast<std::size_t>(origin.input_index) >= inputs.size()) {
    return false;
  }

  const VideoInputInfo& input = inputs[static_cast<std::size_t>(origin.input_index)];
  return input.width == output.width &&
    input.height == output.height &&
    input.format == output.format;
}

inline void copy_video_frame_pixels(
  const PVideoFrame& src,
  const PVideoFrame& dst,
  VideoFormat format
) {
  for (int plane = 0; plane < format.plane_count; ++plane) {
    const int host_plane = plane_id(format, plane);
    const BYTE* src_ptr = src->GetReadPtr(host_plane);
    BYTE* dst_ptr = dst->GetWritePtr(host_plane);
    const int src_pitch = src->GetPitch(host_plane);
    const int dst_pitch = dst->GetPitch(host_plane);
    const int row_size = std::min(src->GetRowSize(host_plane), dst->GetRowSize(host_plane));
    const int height = std::min(src->GetHeight(host_plane), dst->GetHeight(host_plane));

    for (int y = 0; y < height; ++y) {
      std::memcpy(
        dst_ptr + static_cast<std::ptrdiff_t>(y) * dst_pitch,
        src_ptr + static_cast<std::ptrdiff_t>(y) * src_pitch,
        static_cast<std::size_t>(row_size)
      );
    }
  }
}

template <std::size_t InputCount>
class VideoFrameProvider final : public ds::VideoFrameProvider {
public:
  VideoFrameProvider(
    std::span<PClip> clips,
    std::span<const VideoInputInfo> input_infos,
    IScriptEnvironment* env
  ) : clips_(clips),
      input_infos_(input_infos),
      env_(env) {}

  Result<RequestedVideoFrame> get(int input_index, int frame_number) override {
    if (input_index < 0 || input_index >= static_cast<int>(clips_.size())) {
      return Result<RequestedVideoFrame>::failure(
        Error{ErrorCode::InvalidArgument, "DualSynth: video input index is out of range"}
      );
    }

    const auto index = static_cast<std::size_t>(input_index);
    PVideoFrame frame = clips_[index]->GetFrame(frame_number, env_);
    if (!frame) {
      return Result<RequestedVideoFrame>::failure(
        Error{ErrorCode::HostError, "DualSynth: AviSynth did not provide the requested frame"}
      );
    }

    frames_.push_back(frame);
    return Result<RequestedVideoFrame>::success(
      RequestedVideoFrame{
        input_index,
        frame_number,
        make_video_frame_view(frame, input_infos_[index].format)
      }
    );
  }

private:
  std::span<PClip> clips_;
  std::span<const VideoInputInfo> input_infos_;
  IScriptEnvironment* env_;
  std::vector<PVideoFrame> frames_;
};

inline void copy_audio_info(VideoInfo& dst, const VideoInfo& src) {
  dst.audio_samples_per_second = src.audio_samples_per_second;
  dst.sample_type = src.sample_type;
  dst.num_audio_samples = src.num_audio_samples;
  dst.nchannels = src.nchannels;
}

template <class Bridge>
class VideoFilter final : public IClip {
public:
  using Filter = typename Bridge::Core;
  static constexpr auto input_count = static_cast<std::size_t>(Filter::input_count);

  VideoFilter(
    std::array<PClip, input_count> clips,
    std::array<VideoInputInfo, input_count> input_infos,
    VideoOutputInfo output,
    VideoFilterState<Filter> state,
    MtMode mt_mode,
    std::size_t parity_source_index,
    bool forward_audio
  ) : clips_(std::move(clips)),
      input_infos_(input_infos),
      output_format_(output.format),
      state_(std::move(state)),
      mt_mode_(mt_mode),
      parity_source_index_(parity_source_index),
      forward_audio_(forward_audio) {
    vi_ = make_video_info(output);
    if (forward_audio_) {
      copy_audio_info(vi_, clips_[0]->GetVideoInfo());
    }
  }

  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override {
    try {
      PVideoFrame dst = new_output_frame(n, env);
      VideoFrameProvider<input_count> provider(clips_, input_infos_, env);
      const auto result = process_video_filter<Filter>(
        n,
        provider,
        make_mutable_video_frame_view(dst, output_format_),
        state_
      );

      if (!result.has_value()) {
        env->ThrowError(result.error().message.c_str());
      }

      return dst;
    } catch (const AvisynthError&) {
      throw;
    } catch (const std::exception& error) {
      env->ThrowError(error.what());
    } catch (...) {
      env->ThrowError("DualSynth: unhandled exception in AviSynth video wrapper");
    }

    return {};
  }

  bool __stdcall GetParity(int n) override {
    return clips_[parity_source_index_]->GetParity(n);
  }

  void __stdcall GetAudio(void* buf, int64_t start, int64_t count, IScriptEnvironment* env) override {
    try {
      if (!forward_audio_) {
        env->ThrowError("DualSynth: video filter has no audio");
      }
      clips_[0]->GetAudio(buf, start, count, env);
    } catch (const AvisynthError&) {
      throw;
    } catch (const std::exception& error) {
      env->ThrowError(error.what());
    } catch (...) {
      env->ThrowError("DualSynth: unhandled exception in AviSynth video audio forwarding");
    }
  }

  int __stdcall SetCacheHints(int cachehints, int frame_range) override {
    return cache_hints_video_filter<Filter>(
      cachehints,
      frame_range,
      cache_hint_response(cachehints, frame_range, mt_mode_),
      state_
    );
  }

  const VideoInfo& __stdcall GetVideoInfo() override {
    return vi_;
  }

private:
  PVideoFrame new_output_frame(int n, IScriptEnvironment* env) {
    const OutputOrigin origin = filter_output_origin<Filter>();
    if (origin.kind == OutputOriginKind::Fresh) {
      return env->NewVideoFrame(vi_);
    }

    const auto origin_index = static_cast<std::size_t>(origin.input_index);
    PVideoFrame src = clips_[origin_index]->GetFrame(n, env);
    if (origin.kind == OutputOriginKind::TakeFromInput && env->MakeWritable(&src)) {
      return src;
    }

    PVideoFrame dst = env->NewVideoFrame(vi_);
    copy_video_frame_pixels(src, dst, output_format_);
    return dst;
  }

  std::array<PClip, input_count> clips_;
  std::array<VideoInputInfo, input_count> input_infos_;
  VideoInfo vi_{};
  VideoFormat output_format_;
  VideoFilterState<Filter> state_;
  MtMode mt_mode_;
  std::size_t parity_source_index_;
  bool forward_audio_;
};

template <class Bridge>
bool accepts_video_format(VideoFormat format) {
  if constexpr (requires { Bridge::accepts_video_format(format); }) {
    return Bridge::accepts_video_format(format);
  } else {
    return true;
  }
}

inline Result<ParamValues> read_params(
  const AVSValue& args,
  const FilterDescriptor& descriptor
);

template <VideoBridge Bridge>
AVSValue create_video_filter_bridge(AVSValue args, IScriptEnvironment* env) {
  using Filter = typename Bridge::Core;
  constexpr auto input_count = static_cast<std::size_t>(Filter::input_count);

  try {
    std::array<PClip, input_count> clips{};
    std::array<VideoInputInfo, input_count> input_infos{};

    for (std::size_t i = 0; i < input_count; ++i) {
      clips[i] = args[static_cast<int>(i)].AsClip();
      const VideoInfo& vi = clips[i]->GetVideoInfo();
      const auto format = make_video_format(vi);
      if (!vi.HasVideo() || !format.has_value() || !accepts_video_format<Bridge>(format.value())) {
        env->ThrowError(Bridge::avs_format_error);
      }

      input_infos[i] = VideoInputInfo{
        vi.width,
        vi.height,
        vi.num_frames,
        format.value(),
        FrameRate{vi.fps_numerator, vi.fps_denominator}
      };
    }

    const auto collected = collect_video_input_infos<Filter>(input_infos);
    if (!collected.has_value()) {
      env->ThrowError(collected.error().message.c_str());
    }

    auto init_result = [&]() -> Result<VideoFilterInstance<Filter>> {
      if constexpr (requires { Bridge::descriptor(); }) {
        auto params = read_params(args, Bridge::descriptor());
        if (!params.has_value()) {
          return Result<VideoFilterInstance<Filter>>::failure(params.error());
        }
        return init_video_filter_instance<Filter>(
          collected.value(),
          &params.value(),
          host_global_lock_callbacks(env)
        );
      } else {
        return init_video_filter_instance<Filter>(
          collected.value(),
          nullptr,
          host_global_lock_callbacks(env)
        );
      }
    }();
    if (!init_result.has_value()) {
      env->ThrowError(init_result.error().message.c_str());
    }

    const VideoOutputInfo& output = init_result.value().output;
    if (pixel_type(output.format) == VideoInfo::CS_UNKNOWN) {
      env->ThrowError("DualSynth: unsupported AviSynth output format");
    }

    if (!output_origin_matches(filter_output_origin<Filter>(), output, input_infos)) {
      env->ThrowError("DualSynth: output origin is incompatible with output video info");
    }

    return new VideoFilter<Bridge>(
      std::move(clips),
      input_infos,
      output,
      std::move(init_result.value().state),
      bridge_mt_mode<Bridge>(),
      Bridge::parity_source_index,
      Bridge::forward_audio
    );
  } catch (const AvisynthError&) {
    throw;
  } catch (const std::exception& error) {
    env->ThrowError(error.what());
  } catch (...) {
    env->ThrowError("DualSynth: unhandled exception in AviSynth video creation");
  }

  return {};
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
