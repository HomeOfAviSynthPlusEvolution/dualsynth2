#pragma once

#ifndef AVSC_NO_DECLSPEC
#define AVSC_NO_DECLSPEC
#endif

#include <avisynth_c.h>

#include <dualsynth/error.hpp>
#include <dualsynth/format.hpp>
#include <dualsynth/frame.hpp>
#include <dualsynth/global_lock.hpp>
#include <dualsynth/host_variable.hpp>
#include <dualsynth/param.hpp>
#include <dualsynth/video_bridge.hpp>
#include <dualsynth/video_filter.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace ds::avisynth {
enum class MtMode {
  NiceFilter,
  MultiInstance,
  Serialized
};
}

namespace ds::avisynth::c {

using ds::avisynth::MtMode;

struct CApi {
  using avs_add_function_fn = int(AVSC_CC*)(AVS_ScriptEnvironment*, const char*, const char*, AVS_ApplyFunc, void*);
  using avs_take_clip_fn = AVS_Clip*(AVSC_CC*)(AVS_Value, AVS_ScriptEnvironment*);
  using avs_get_video_info_fn = const AVS_VideoInfo*(AVSC_CC*)(AVS_Clip*);
  using avs_release_clip_fn = void(AVSC_CC*)(AVS_Clip*);
  using avs_new_c_filter_fn = AVS_Clip*(AVSC_CC*)(AVS_ScriptEnvironment*, AVS_FilterInfo**, AVS_Value, int);
  using avs_set_to_clip_fn = void(AVSC_CC*)(AVS_Value*, AVS_Clip*);
  using avs_get_frame_fn = AVS_VideoFrame*(AVSC_CC*)(AVS_Clip*, int);
  using avs_make_writable_fn = int(AVSC_CC*)(AVS_ScriptEnvironment*, AVS_VideoFrame**);
  using avs_new_video_frame_p_fn = AVS_VideoFrame*(AVSC_CC*)(AVS_ScriptEnvironment*, const AVS_VideoInfo*, const AVS_VideoFrame*);
  using avs_new_video_frame_a_fn = AVS_VideoFrame*(AVSC_CC*)(AVS_ScriptEnvironment*, const AVS_VideoInfo*, int);
  using avs_release_video_frame_fn = void(AVSC_CC*)(AVS_VideoFrame*);
  using avs_get_read_ptr_p_fn = const BYTE*(AVSC_CC*)(const AVS_VideoFrame*, int);
  using avs_get_write_ptr_p_fn = BYTE*(AVSC_CC*)(const AVS_VideoFrame*, int);
  using avs_get_pitch_p_fn = int(AVSC_CC*)(const AVS_VideoFrame*, int);
  using avs_get_row_size_p_fn = int(AVSC_CC*)(const AVS_VideoFrame*, int);
  using avs_get_height_p_fn = int(AVSC_CC*)(const AVS_VideoFrame*, int);
  using avs_get_parity_fn = int(AVSC_CC*)(AVS_Clip*, int);
  using avs_get_audio_fn = int(AVSC_CC*)(AVS_Clip*, void*, int64_t, int64_t);

  avs_add_function_fn add_function{nullptr};
  avs_take_clip_fn take_clip{nullptr};
  avs_get_video_info_fn get_video_info{nullptr};
  avs_release_clip_fn release_clip{nullptr};
  avs_new_c_filter_fn new_c_filter{nullptr};
  avs_set_to_clip_fn set_to_clip{nullptr};
  avs_get_frame_fn get_frame{nullptr};
  avs_make_writable_fn make_writable{nullptr};
  avs_new_video_frame_p_fn new_video_frame_p{nullptr};
  avs_new_video_frame_a_fn new_video_frame_a{nullptr};
  avs_release_video_frame_fn release_video_frame{nullptr};
  avs_get_read_ptr_p_fn get_read_ptr_p{nullptr};
  avs_get_write_ptr_p_fn get_write_ptr_p{nullptr};
  avs_get_pitch_p_fn get_pitch_p{nullptr};
  avs_get_row_size_p_fn get_row_size_p{nullptr};
  avs_get_height_p_fn get_height_p{nullptr};
  avs_get_parity_fn get_parity{nullptr};
  avs_get_audio_fn get_audio{nullptr};

  bool loaded{false};

  static CApi& instance() {
    static CApi api;
    if (!api.loaded) {
      api.init();
    }
    return api;
  }

  void init() {
#if defined(_WIN32)
    HMODULE h = GetModuleHandleA("avisynth.dll");
    if (!h) h = GetModuleHandleA("avisynth");
    if (!h) h = GetModuleHandleA(nullptr);
    if (!h) return;
    auto sym = [h](const char* name) {
      return reinterpret_cast<void*>(GetProcAddress(h, name));
    };
#else
    void* h = dlopen(nullptr, RTLD_NOW | RTLD_GLOBAL);
    if (!h) return;
    auto sym = [h](const char* name) {
      return dlsym(h, name);
    };
#endif
    add_function = reinterpret_cast<avs_add_function_fn>(sym("avs_add_function"));
    take_clip = reinterpret_cast<avs_take_clip_fn>(sym("avs_take_clip"));
    get_video_info = reinterpret_cast<avs_get_video_info_fn>(sym("avs_get_video_info"));
    release_clip = reinterpret_cast<avs_release_clip_fn>(sym("avs_release_clip"));
    new_c_filter = reinterpret_cast<avs_new_c_filter_fn>(sym("avs_new_c_filter"));
    set_to_clip = reinterpret_cast<avs_set_to_clip_fn>(sym("avs_set_to_clip"));
    get_frame = reinterpret_cast<avs_get_frame_fn>(sym("avs_get_frame"));
    make_writable = reinterpret_cast<avs_make_writable_fn>(sym("avs_make_writable"));
    new_video_frame_p = reinterpret_cast<avs_new_video_frame_p_fn>(sym("avs_new_video_frame_p"));
    new_video_frame_a = reinterpret_cast<avs_new_video_frame_a_fn>(sym("avs_new_video_frame_a"));
    release_video_frame = reinterpret_cast<avs_release_video_frame_fn>(sym("avs_release_video_frame"));
    get_read_ptr_p = reinterpret_cast<avs_get_read_ptr_p_fn>(sym("avs_get_read_ptr_p"));
    if (!get_read_ptr_p) get_read_ptr_p = reinterpret_cast<avs_get_read_ptr_p_fn>(sym("avs_get_read_ptr"));
    get_write_ptr_p = reinterpret_cast<avs_get_write_ptr_p_fn>(sym("avs_get_write_ptr_p"));
    if (!get_write_ptr_p) get_write_ptr_p = reinterpret_cast<avs_get_write_ptr_p_fn>(sym("avs_get_write_ptr"));
    get_pitch_p = reinterpret_cast<avs_get_pitch_p_fn>(sym("avs_get_pitch_p"));
    if (!get_pitch_p) get_pitch_p = reinterpret_cast<avs_get_pitch_p_fn>(sym("avs_get_pitch"));
    get_row_size_p = reinterpret_cast<avs_get_row_size_p_fn>(sym("avs_get_row_size_p"));
    if (!get_row_size_p) get_row_size_p = reinterpret_cast<avs_get_row_size_p_fn>(sym("avs_get_row_size"));
    get_height_p = reinterpret_cast<avs_get_height_p_fn>(sym("avs_get_height_p"));
    if (!get_height_p) get_height_p = reinterpret_cast<avs_get_height_p_fn>(sym("avs_get_height"));
    get_parity = reinterpret_cast<avs_get_parity_fn>(sym("avs_get_parity"));
    get_audio = reinterpret_cast<avs_get_audio_fn>(sym("avs_get_audio"));
    loaded = true;
  }
};

inline AVS_Value avs_new_value_error(const char* message) {
  AVS_Value v;
  v.type = 'e';
  v.array_size = 1;
  v.d.string = message;
  return v;
}

inline AVS_VideoFrame* new_video_frame(AVS_ScriptEnvironment* env, const AVS_VideoInfo* vi) {
  auto& api = CApi::instance();
  return api.new_video_frame_a ? api.new_video_frame_a(env, vi, AVS_FRAME_ALIGN) : nullptr;
}

inline BYTE* get_write_ptr_p(const AVS_VideoFrame* frame, int plane) {
  auto& api = CApi::instance();
  return api.get_write_ptr_p ? api.get_write_ptr_p(frame, plane) : nullptr;
}

inline const BYTE* get_read_ptr_p(const AVS_VideoFrame* frame, int plane) {
  auto& api = CApi::instance();
  return api.get_read_ptr_p ? api.get_read_ptr_p(frame, plane) : nullptr;
}

inline int get_pitch_p(const AVS_VideoFrame* frame, int plane) {
  auto& api = CApi::instance();
  return api.get_pitch_p ? api.get_pitch_p(frame, plane) : 0;
}

inline int get_row_size_p(const AVS_VideoFrame* frame, int plane) {
  auto& api = CApi::instance();
  return api.get_row_size_p ? api.get_row_size_p(frame, plane) : 0;
}

inline int get_height_p(const AVS_VideoFrame* frame, int plane) {
  auto& api = CApi::instance();
  return api.get_height_p ? api.get_height_p(frame, plane) : 0;
}

inline int add_function(AVS_ScriptEnvironment* env, const char* name, const char* params, AVS_ApplyFunc apply, void* user_data) {
  auto& api = CApi::instance();
  return api.add_function ? api.add_function(env, name, params, apply, user_data) : 0;
}

inline AVS_Clip* new_c_filter(AVS_ScriptEnvironment* env, AVS_FilterInfo** fi, AVS_Value child, int store_child) {
  auto& api = CApi::instance();
  return api.new_c_filter ? api.new_c_filter(env, fi, child, store_child) : nullptr;
}

inline void release_clip(AVS_Clip* clip) {
  auto& api = CApi::instance();
  if (clip && api.release_clip) {
    api.release_clip(clip);
  }
}

inline AVS_Clip* take_clip(AVS_Value val, AVS_ScriptEnvironment* env) {
  auto& api = CApi::instance();
  return api.take_clip ? api.take_clip(val, env) : nullptr;
}

inline const AVS_VideoInfo* get_video_info(AVS_Clip* clip) {
  auto& api = CApi::instance();
  return api.get_video_info ? api.get_video_info(clip) : nullptr;
}

inline int get_audio(AVS_Clip* clip, void* buf, int64_t start, int64_t count) {
  auto& api = CApi::instance();
  return api.get_audio ? api.get_audio(clip, buf, start, count) : -1;
}

inline void set_to_clip(AVS_Value* val, AVS_Clip* clip) {
  auto& api = CApi::instance();
  if (api.set_to_clip) {
    api.set_to_clip(val, clip);
  } else if (val) {
    val->type = 'c';
    val->array_size = 1;
    val->d.clip = clip;
  }
}

template <class Filter, class = void>
struct filter_has_output_origin : std::false_type {};

template <class Filter>
struct filter_has_output_origin<Filter, std::void_t<decltype(Filter::output_origin)>> : std::true_type {};

template <class Filter>
constexpr OutputOrigin filter_output_origin() {
  if constexpr (filter_has_output_origin<Filter>::value) {
    return Filter::output_origin;
  } else {
    return OutputOrigin::fresh();
  }
}

inline int c_host_mt_mode(MtMode mode) {
  switch (mode) {
  case MtMode::NiceFilter:
    return AVS_MT_NICE_FILTER;
  case MtMode::MultiInstance:
    return AVS_MT_MULTI_INSTANCE;
  case MtMode::Serialized:
    return AVS_MT_SERIALIZED;
  }
  return AVS_MT_SERIALIZED;
}

inline int host_mt_mode(MtMode mode) {
  return c_host_mt_mode(mode);
}

template <class Bridge, class = void>
struct bridge_has_avs_mt_mode : std::false_type {};

template <class Bridge>
struct bridge_has_avs_mt_mode<Bridge, std::void_t<decltype(Bridge::avs_mt_mode)>> : std::true_type {};

template <class Bridge>
constexpr MtMode bridge_mt_mode() {
  if constexpr (bridge_has_avs_mt_mode<Bridge>::value) {
    return Bridge::avs_mt_mode;
  } else {
    return MtMode::NiceFilter;
  }
}

template <class Bridge, class Format, class = void>
struct bridge_has_accepts_video_format : std::false_type {};

template <class Bridge, class Format>
struct bridge_has_accepts_video_format<
  Bridge,
  Format,
  std::void_t<decltype(Bridge::accepts_video_format(std::declval<Format>()))>
> : std::true_type {};

template <class Bridge>
bool accepts_video_format(VideoFormat format) {
  if constexpr (bridge_has_accepts_video_format<Bridge, VideoFormat>::value) {
    return Bridge::accepts_video_format(format);
  } else {
    return true;
  }
}

template <class Bridge, class = void>
struct bridge_has_descriptor : std::false_type {};

template <class Bridge>
struct bridge_has_descriptor<
  Bridge,
  std::void_t<decltype(Bridge::descriptor())>
> : std::true_type {};

template <class Bridge>
inline const char* bridge_avs_signature() {
  if constexpr (bridge_has_descriptor<Bridge>::value) {
    static const std::string sig = [] {
      auto res = make_avisynth_signature(Bridge::descriptor());
      return res.has_value() ? res.value() : std::string(Bridge::avs_signature);
    }();
    return sig.c_str();
  } else {
    return Bridge::avs_signature;
  }
}

inline int plane_id(VideoFormat format, int plane) {
  if (format.color_family == ColorFamily::Rgb) {
    static constexpr std::array<int, 4> rgb_planes{AVS_PLANAR_R, AVS_PLANAR_G, AVS_PLANAR_B, AVS_PLANAR_A};
    return rgb_planes[static_cast<std::size_t>(plane)];
  }

  static constexpr std::array<int, 4> yuv_planes{AVS_PLANAR_Y, AVS_PLANAR_U, AVS_PLANAR_V, AVS_PLANAR_A};
  return yuv_planes[static_cast<std::size_t>(plane)];
}

inline int yuv_pixel_type(VideoFormat format) {
  if (format.plane_count != 3 && format.plane_count != 4) {
    return AVS_CS_UNKNOWN;
  }

  const bool has_alpha = format.plane_count == 4;

  if (format.subsampling_w == 0 && format.subsampling_h == 0) {
    switch (format.sample_format) {
    case SampleFormat::UInt8:
      return has_alpha ? AVS_CS_YUVA444 : AVS_CS_YV24;
    case SampleFormat::UInt10:
      return has_alpha ? AVS_CS_YUVA444P10 : AVS_CS_YUV444P10;
    case SampleFormat::UInt12:
      return has_alpha ? AVS_CS_YUVA444P12 : AVS_CS_YUV444P12;
    case SampleFormat::UInt14:
      return has_alpha ? AVS_CS_YUVA444P14 : AVS_CS_YUV444P14;
    case SampleFormat::UInt16:
      return has_alpha ? AVS_CS_YUVA444P16 : AVS_CS_YUV444P16;
    case SampleFormat::Float32:
      return has_alpha ? AVS_CS_YUVA444PS : AVS_CS_YUV444PS;
    }
  }

  if (format.subsampling_w == 1 && format.subsampling_h == 0) {
    switch (format.sample_format) {
    case SampleFormat::UInt8:
      return has_alpha ? AVS_CS_YUVA422 : AVS_CS_YV16;
    case SampleFormat::UInt10:
      return has_alpha ? AVS_CS_YUVA422P10 : AVS_CS_YUV422P10;
    case SampleFormat::UInt12:
      return has_alpha ? AVS_CS_YUVA422P12 : AVS_CS_YUV422P12;
    case SampleFormat::UInt14:
      return has_alpha ? AVS_CS_YUVA422P14 : AVS_CS_YUV422P14;
    case SampleFormat::UInt16:
      return has_alpha ? AVS_CS_YUVA422P16 : AVS_CS_YUV422P16;
    case SampleFormat::Float32:
      return has_alpha ? AVS_CS_YUVA422PS : AVS_CS_YUV422PS;
    }
  }

  if (format.subsampling_w == 1 && format.subsampling_h == 1) {
    switch (format.sample_format) {
    case SampleFormat::UInt8:
      return has_alpha ? AVS_CS_YUVA420 : AVS_CS_YV12;
    case SampleFormat::UInt10:
      return has_alpha ? AVS_CS_YUVA420P10 : AVS_CS_YUV420P10;
    case SampleFormat::UInt12:
      return has_alpha ? AVS_CS_YUVA420P12 : AVS_CS_YUV420P12;
    case SampleFormat::UInt14:
      return has_alpha ? AVS_CS_YUVA420P14 : AVS_CS_YUV420P14;
    case SampleFormat::UInt16:
      return has_alpha ? AVS_CS_YUVA420P16 : AVS_CS_YUV420P16;
    case SampleFormat::Float32:
      return has_alpha ? AVS_CS_YUVA420PS : AVS_CS_YUV420PS;
    }
  }

  return AVS_CS_UNKNOWN;
}

inline int pixel_type(VideoFormat format) {
  if (format.color_family == ColorFamily::Gray) {
    switch (format.sample_format) {
    case SampleFormat::UInt8:
      return AVS_CS_Y8;
    case SampleFormat::UInt10:
      return AVS_CS_Y10;
    case SampleFormat::UInt12:
      return AVS_CS_Y12;
    case SampleFormat::UInt14:
      return AVS_CS_Y14;
    case SampleFormat::UInt16:
      return AVS_CS_Y16;
    case SampleFormat::Float32:
      return AVS_CS_Y32;
    }
  }

  if (format.color_family == ColorFamily::Rgb && format.plane_count == 3) {
    switch (format.sample_format) {
    case SampleFormat::UInt8:
      return AVS_CS_RGBP;
    case SampleFormat::UInt10:
      return AVS_CS_RGBP10;
    case SampleFormat::UInt12:
      return AVS_CS_RGBP12;
    case SampleFormat::UInt14:
      return AVS_CS_RGBP14;
    case SampleFormat::UInt16:
      return AVS_CS_RGBP16;
    case SampleFormat::Float32:
      return AVS_CS_RGBPS;
    }
  }

  if (format.color_family == ColorFamily::Rgb && format.plane_count == 4) {
    switch (format.sample_format) {
    case SampleFormat::UInt8:
      return AVS_CS_RGBAP;
    case SampleFormat::UInt10:
      return AVS_CS_RGBAP10;
    case SampleFormat::UInt12:
      return AVS_CS_RGBAP12;
    case SampleFormat::UInt14:
      return AVS_CS_RGBAP14;
    case SampleFormat::UInt16:
      return AVS_CS_RGBAP16;
    case SampleFormat::Float32:
      return AVS_CS_RGBAPS;
    }
  }

  if (format.color_family == ColorFamily::Yuv) {
    return yuv_pixel_type(format);
  }

  return AVS_CS_UNKNOWN;
}

inline Result<SampleFormat> sample_format_from_pixel_type(int pixel_type) {
  switch (pixel_type & AVS_CS_SAMPLE_BITS_MASK) {
  case AVS_CS_SAMPLE_BITS_8:
    return Result<SampleFormat>::success(SampleFormat::UInt8);
  case AVS_CS_SAMPLE_BITS_10:
    return Result<SampleFormat>::success(SampleFormat::UInt10);
  case AVS_CS_SAMPLE_BITS_12:
    return Result<SampleFormat>::success(SampleFormat::UInt12);
  case AVS_CS_SAMPLE_BITS_14:
    return Result<SampleFormat>::success(SampleFormat::UInt14);
  case AVS_CS_SAMPLE_BITS_16:
    return Result<SampleFormat>::success(SampleFormat::UInt16);
  case AVS_CS_SAMPLE_BITS_32:
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
  if (pixel_type == AVS_CS_UNKNOWN) {
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
    (pixel_type & AVS_CS_PLANAR) &&
    (pixel_type & AVS_CS_BGR) &&
    (pixel_type & (AVS_CS_RGB_TYPE | AVS_CS_RGBA_TYPE))
  ) {
    const int plane_count = (pixel_type & AVS_CS_RGBA_TYPE) ? 4 : 3;
    return Result<VideoFormat>::success(
      VideoFormat{ColorFamily::Rgb, sample_format.value(), plane_count, 0, 0}
    );
  }

  if (pixel_type & AVS_CS_YUVA) {
    const int subsampling_w = subsampling_from_pixel_type(
      pixel_type,
      AVS_CS_SUB_WIDTH_MASK,
      AVS_CS_SUB_WIDTH_1,
      AVS_CS_SUB_WIDTH_2,
      AVS_CS_SUB_WIDTH_4
    );
    const int subsampling_h = subsampling_from_pixel_type(
      pixel_type,
      AVS_CS_SUB_HEIGHT_MASK,
      AVS_CS_SUB_HEIGHT_1,
      AVS_CS_SUB_HEIGHT_2,
      AVS_CS_SUB_HEIGHT_4
    );
    return Result<VideoFormat>::success(
      VideoFormat{ColorFamily::Yuv, sample_format.value(), 4, subsampling_w, subsampling_h}
    );
  }

  if (
    (pixel_type & AVS_CS_PLANAR) &&
    (pixel_type & AVS_CS_YUV) &&
    (pixel_type & AVS_CS_INTERLEAVED)
  ) {
    return Result<VideoFormat>::success(
      VideoFormat{ColorFamily::Gray, sample_format.value(), 1, 0, 0}
    );
  }

  if (
    (pixel_type & AVS_CS_PLANAR) &&
    (pixel_type & AVS_CS_YUV) &&
    !(pixel_type & AVS_CS_INTERLEAVED)
  ) {
    const int subsampling_w = subsampling_from_pixel_type(
      pixel_type,
      AVS_CS_SUB_WIDTH_MASK,
      AVS_CS_SUB_WIDTH_1,
      AVS_CS_SUB_WIDTH_2,
      AVS_CS_SUB_WIDTH_4
    );
    const int subsampling_h = subsampling_from_pixel_type(
      pixel_type,
      AVS_CS_SUB_HEIGHT_MASK,
      AVS_CS_SUB_HEIGHT_1,
      AVS_CS_SUB_HEIGHT_2,
      AVS_CS_SUB_HEIGHT_4
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

inline Result<VideoFormat> make_video_format(const AVS_VideoInfo& vi) {
  return video_format_from_pixel_type(vi.pixel_type);
}

inline void initialize_no_audio(AVS_VideoInfo& vi) {
  vi.audio_samples_per_second = 0;
  vi.sample_type = 0;
  vi.num_audio_samples = 0;
  vi.nchannels = 0;
}

inline void initialize_no_video(AVS_VideoInfo& vi) {
  vi.width = 0;
  vi.height = 0;
  vi.fps_numerator = 0;
  vi.fps_denominator = 1;
  vi.num_frames = 0;
  vi.pixel_type = AVS_CS_UNKNOWN;
}

inline AVS_VideoInfo make_video_info(const VideoOutputInfo& output) {
  AVS_VideoInfo vi{};
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
  const AVS_VideoFrame* frame,
  VideoFormat format
) {
  auto& api = CApi::instance();
  std::array<PlaneView, 4> planes{};
  const int sample_bytes = bytes_per_sample(format.sample_format);
  for (int plane = 0; plane < format.plane_count; ++plane) {
    const int host_plane = plane_id(format, plane);
    planes[static_cast<std::size_t>(plane)] = PlaneView{
      api.get_read_ptr_p ? api.get_read_ptr_p(frame, host_plane) : nullptr,
      api.get_pitch_p ? api.get_pitch_p(frame, host_plane) : 0,
      api.get_row_size_p ? api.get_row_size_p(frame, host_plane) / sample_bytes : 0,
      api.get_height_p ? api.get_height_p(frame, host_plane) : 0
    };
  }
  return VideoFrameView{format, format.plane_count, planes};
}

inline MutableVideoFrameView make_mutable_video_frame_view(
  AVS_VideoFrame* frame,
  VideoFormat format
) {
  auto& api = CApi::instance();
  std::array<MutablePlaneView, 4> planes{};
  const int sample_bytes = bytes_per_sample(format.sample_format);
  for (int plane = 0; plane < format.plane_count; ++plane) {
    const int host_plane = plane_id(format, plane);
    planes[static_cast<std::size_t>(plane)] = MutablePlaneView{
      api.get_write_ptr_p ? api.get_write_ptr_p(frame, host_plane) : nullptr,
      api.get_pitch_p ? api.get_pitch_p(frame, host_plane) : 0,
      api.get_row_size_p ? api.get_row_size_p(frame, host_plane) / sample_bytes : 0,
      api.get_height_p ? api.get_height_p(frame, host_plane) : 0
    };
  }
  return MutableVideoFrameView{format, format.plane_count, planes};
}

inline bool output_origin_matches(
  OutputOrigin origin,
  const VideoOutputInfo& output,
  Span<const VideoInputInfo> inputs
) {
  if (origin.pixels == OutputPixelPolicy::Fresh) {
    if (origin.prop_input_index >= 0) {
      if (static_cast<std::size_t>(origin.prop_input_index) >= inputs.size()) {
        return false;
      }
    }
    return true;
  }
  if (origin.pixel_input_index < 0 || static_cast<std::size_t>(origin.pixel_input_index) >= inputs.size()) {
    return false;
  }
  if (origin.prop_input_index >= 0 && static_cast<std::size_t>(origin.prop_input_index) >= inputs.size()) {
    return false;
  }

  const VideoInputInfo& input = inputs[static_cast<std::size_t>(origin.pixel_input_index)];
  return input.width == output.width &&
    input.height == output.height &&
    input.format == output.format;
}

inline void copy_video_frame_pixels(
  const AVS_VideoFrame* src,
  AVS_VideoFrame* dst,
  VideoFormat format
) {
  auto& api = CApi::instance();
  if (!api.get_read_ptr_p || !api.get_write_ptr_p || !api.get_pitch_p || !api.get_row_size_p || !api.get_height_p) {
    return;
  }

  for (int plane = 0; plane < format.plane_count; ++plane) {
    const int host_plane = plane_id(format, plane);
    const BYTE* src_ptr = api.get_read_ptr_p(src, host_plane);
    BYTE* dst_ptr = api.get_write_ptr_p(dst, host_plane);
    const int src_pitch = api.get_pitch_p(src, host_plane);
    const int dst_pitch = api.get_pitch_p(dst, host_plane);
    const int row_size = std::min(api.get_row_size_p(src, host_plane), api.get_row_size_p(dst, host_plane));
    const int height = std::min(api.get_height_p(src, host_plane), api.get_height_p(dst, host_plane));

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
class CVideoFrameProvider final : public ds::VideoFrameProvider {
public:
  CVideoFrameProvider(
    Span<AVS_Clip*> clips,
    Span<const VideoInputInfo> input_infos,
    AVS_ScriptEnvironment* env
  ) : clips_(clips),
      input_infos_(input_infos),
      env_(env) {}

  ~CVideoFrameProvider() override {
    auto& api = CApi::instance();
    for (auto* frame : frames_) {
      if (frame && api.release_video_frame) {
        api.release_video_frame(frame);
      }
    }
  }

  Result<RequestedVideoFrame> get(int input_index, int frame_number) override {
    if (input_index < 0 || input_index >= static_cast<int>(clips_.size())) {
      return Result<RequestedVideoFrame>::failure(
        Error{ErrorCode::InvalidArgument, "DualSynth C: video input index is out of range"}
      );
    }

    auto& api = CApi::instance();
    if (!api.get_frame) {
      return Result<RequestedVideoFrame>::failure(
        Error{ErrorCode::HostError, "DualSynth C: avs_get_frame API function not found"}
      );
    }

    const auto index = static_cast<std::size_t>(input_index);
    AVS_VideoFrame* frame = api.get_frame(clips_[index], frame_number);
    if (!frame) {
      return Result<RequestedVideoFrame>::failure(
        Error{ErrorCode::HostError, "DualSynth C: AviSynth did not provide the requested frame"}
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
  Span<AVS_Clip*> clips_;
  Span<const VideoInputInfo> input_infos_;
  AVS_ScriptEnvironment* env_;
  std::vector<AVS_VideoFrame*> frames_;
};

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

class AvisynthCValueParamSource {
public:
  explicit AvisynthCValueParamSource(AVS_Value args) : args_(args) {}

  bool defined(int index) const {
    if (args_.type != 'a') {
      return index == 0 && avs_defined(args_);
    }
    if (index < 0 || index >= args_.array_size) {
      return false;
    }
    return avs_defined(args_.d.array[index]);
  }

  std::int64_t as_int(int index) const {
    const AVS_Value elt = get_elt(index);
    return avs_as_long(elt);
  }

  double as_float(int index) const {
    const AVS_Value elt = get_elt(index);
    return avs_as_float(elt);
  }

  bool as_bool(int index) const {
    const AVS_Value elt = get_elt(index);
    return avs_as_bool(elt);
  }

  std::string as_string(int index) const {
    const AVS_Value elt = get_elt(index);
    const char* str = avs_as_string(elt);
    return str ? std::string(str) : std::string{};
  }

  std::vector<std::int64_t> as_int_array(int index) const {
    const AVS_Value elt = get_elt(index);
    std::vector<std::int64_t> output;
    if (elt.type == 'a') {
      output.reserve(static_cast<std::size_t>(elt.array_size));
      for (int i = 0; i < elt.array_size; ++i) {
        output.push_back(avs_as_long(elt.d.array[i]));
      }
    }
    return output;
  }

  std::vector<double> as_float_array(int index) const {
    const AVS_Value elt = get_elt(index);
    std::vector<double> output;
    if (elt.type == 'a') {
      output.reserve(static_cast<std::size_t>(elt.array_size));
      for (int i = 0; i < elt.array_size; ++i) {
        output.push_back(avs_as_float(elt.d.array[i]));
      }
    }
    return output;
  }

  std::vector<bool> as_bool_array(int index) const {
    const AVS_Value elt = get_elt(index);
    std::vector<bool> output;
    if (elt.type == 'a') {
      output.reserve(static_cast<std::size_t>(elt.array_size));
      for (int i = 0; i < elt.array_size; ++i) {
        output.push_back(avs_as_bool(elt.d.array[i]));
      }
    }
    return output;
  }

  std::vector<std::string> as_string_array(int index) const {
    const AVS_Value elt = get_elt(index);
    std::vector<std::string> output;
    if (elt.type == 'a') {
      output.reserve(static_cast<std::size_t>(elt.array_size));
      for (int i = 0; i < elt.array_size; ++i) {
        const char* str = avs_as_string(elt.d.array[i]);
        output.emplace_back(str ? str : "");
      }
    }
    return output;
  }

private:
  AVS_Value get_elt(int index) const {
    if (args_.type == 'a') {
      if (index >= 0 && index < args_.array_size) {
        return args_.d.array[index];
      }
      return avs_void;
    }
    return index == 0 ? args_ : avs_void;
  }

  AVS_Value args_;
};

inline Result<ParamValues> read_params(
  const AVS_Value& args,
  const FilterDescriptor& descriptor
) {
  return read_params_from_source(AvisynthCValueParamSource{args}, descriptor);
}

template <class Bridge>
struct CVideoFilterStateHolder {
  using Filter = typename Bridge::Core;
  static constexpr auto input_count = static_cast<std::size_t>(Filter::input_count);

  std::array<AVS_Clip*, input_count> clips{};
  std::array<VideoInputInfo, input_count> input_infos{};
  AVS_VideoInfo vi{};
  VideoFormat output_format{};
  VideoFilterState<Filter> state{};
  MtMode mt_mode{MtMode::NiceFilter};
  std::size_t parity_source_index{0};
  bool forward_audio{false};
  AVS_ScriptEnvironment* env{nullptr};

  ~CVideoFilterStateHolder() {
    auto& api = CApi::instance();
    for (auto* clip : clips) {
      if (clip && api.release_clip) {
        api.release_clip(clip);
      }
    }
  }
};

template <class Bridge>
AVS_VideoFrame* AVSC_CC c_filter_get_frame(AVS_FilterInfo* fi, int n) {
  using Filter = typename Bridge::Core;
  constexpr auto input_count = static_cast<std::size_t>(Filter::input_count);
  auto* holder = static_cast<CVideoFilterStateHolder<Bridge>*>(fi->user_data);
  if (!holder) {
    fi->error = "DualSynth C: filter user_data is null";
    return nullptr;
  }

  auto& api = CApi::instance();
  try {
    AVS_VideoFrame* dst = nullptr;
    const OutputOrigin origin = filter_output_origin<Filter>();
    if ((origin.pixels == OutputPixelPolicy::TakeFromInput || origin.pixels == OutputPixelPolicy::CopyFromInput) && origin.pixel_input_index >= 0) {
      const auto origin_idx = static_cast<std::size_t>(origin.pixel_input_index);
      AVS_VideoFrame* src = api.get_frame ? api.get_frame(holder->clips[origin_idx], n) : nullptr;
      if (src) {
        dst = api.new_video_frame_p ? api.new_video_frame_p(fi->env, &holder->vi, src)
                                    : (api.new_video_frame_a ? api.new_video_frame_a(fi->env, &holder->vi, AVS_FRAME_ALIGN) : nullptr);
        copy_video_frame_pixels(src, dst, holder->output_format);
        if (api.release_video_frame) {
          api.release_video_frame(src);
        }
      }
    } else {
      AVS_VideoFrame* prop_src = nullptr;
      if (origin.prop_input_index >= 0 && static_cast<std::size_t>(origin.prop_input_index) < holder->clips.size() && api.get_frame) {
        prop_src = api.get_frame(holder->clips[static_cast<std::size_t>(origin.prop_input_index)], n);
      }
      dst = api.new_video_frame_p ? api.new_video_frame_p(fi->env, &holder->vi, prop_src)
                                  : (api.new_video_frame_a ? api.new_video_frame_a(fi->env, &holder->vi, AVS_FRAME_ALIGN) : nullptr);
      if (prop_src && api.release_video_frame) {
        api.release_video_frame(prop_src);
      }
    }

    if (!dst) {
      fi->error = "DualSynth C: failed to allocate output video frame";
      return nullptr;
    }

    CVideoFrameProvider<input_count> provider(holder->clips, holder->input_infos, fi->env);
    const auto result = process_video_filter<Filter>(
      n,
      provider,
      make_mutable_video_frame_view(dst, holder->output_format),
      holder->state
    );

    if (!result.has_value()) {
      fi->error = result.error().message.c_str();
      if (api.release_video_frame) {
        api.release_video_frame(dst);
      }
      return nullptr;
    }

    return dst;
  } catch (const std::exception& error) {
    fi->error = error.what();
    return nullptr;
  } catch (...) {
    fi->error = "DualSynth C: unhandled exception in get_frame";
    return nullptr;
  }
}

template <class Bridge>
int AVSC_CC c_filter_get_parity(AVS_FilterInfo* fi, int n) {
  auto* holder = static_cast<CVideoFilterStateHolder<Bridge>*>(fi->user_data);
  auto& api = CApi::instance();
  if (!holder || holder->parity_source_index >= holder->clips.size() || !holder->clips[holder->parity_source_index] || !api.get_parity) {
    return 0;
  }
  return api.get_parity(holder->clips[holder->parity_source_index], n);
}

template <class Bridge>
int AVSC_CC c_filter_get_audio(AVS_FilterInfo* fi, void* buf, int64_t start, int64_t count) {
  auto* holder = static_cast<CVideoFilterStateHolder<Bridge>*>(fi->user_data);
  auto& api = CApi::instance();
  if (!holder || !holder->forward_audio || holder->clips.empty() || !holder->clips[0] || !api.get_audio) {
    fi->error = "DualSynth C: video filter has no audio";
    return -1;
  }
  return api.get_audio(holder->clips[0], buf, start, count);
}

template <class Bridge>
int AVSC_CC c_filter_set_cache_hints(AVS_FilterInfo* fi, int cachehints, int frame_range) {
  using Filter = typename Bridge::Core;
  auto* holder = static_cast<CVideoFilterStateHolder<Bridge>*>(fi->user_data);
  if (!holder) {
    return 0;
  }
  int default_response = 0;
  if (cachehints == AVS_CACHE_GET_MTMODE) {
    default_response = c_host_mt_mode(holder->mt_mode);
  }
  return cache_hints_video_filter<Filter>(
    cachehints,
    frame_range,
    default_response,
    holder->state
  );
}

template <class Bridge>
void AVSC_CC c_filter_free(AVS_FilterInfo* fi) {
  delete static_cast<CVideoFilterStateHolder<Bridge>*>(fi->user_data);
  fi->user_data = nullptr;
}

template <DS_CONCEPT_VIDEO_BRIDGE Bridge>
AVS_Value AVSC_CC create_video_filter_bridge(AVS_ScriptEnvironment* env, AVS_Value args, void*) {
  using Filter = typename Bridge::Core;
  constexpr auto input_count = static_cast<std::size_t>(Filter::input_count);
  auto& api = CApi::instance();

  try {
    std::array<AVS_Clip*, input_count> clips{};
    std::array<VideoInputInfo, input_count> input_infos{};

    for (std::size_t i = 0; i < input_count; ++i) {
      AVS_Value clip_val = (args.type == 'a') ? avs_array_elt(args, static_cast<int>(i)) : args;
      if (!avs_is_clip(clip_val)) {
        return avs_new_value_error(Bridge::missing_input_error);
      }
      if (!api.take_clip || !api.get_video_info) {
        return avs_new_value_error("DualSynth C: take_clip API not found");
      }
      clips[i] = api.take_clip(clip_val, env);
      if (!clips[i]) {
        return avs_new_value_error("DualSynth C: failed to take input clip");
      }
      const AVS_VideoInfo* vi = api.get_video_info(clips[i]);
      if (!vi || !avs_has_video(vi)) {
        for (std::size_t j = 0; j <= i; ++j) {
          if (clips[j] && api.release_clip) api.release_clip(clips[j]);
        }
        return avs_new_value_error(Bridge::avs_format_error);
      }
      const auto format = make_video_format(*vi);
      if (!format.has_value() || !accepts_video_format<Bridge>(format.value())) {
        for (std::size_t j = 0; j <= i; ++j) {
          if (clips[j] && api.release_clip) api.release_clip(clips[j]);
        }
        return avs_new_value_error(Bridge::avs_format_error);
      }

      input_infos[i] = VideoInputInfo{
        vi->width,
        vi->height,
        vi->num_frames,
        format.value(),
        FrameRate{vi->fps_numerator, vi->fps_denominator}
      };
    }

    const auto collected = collect_video_input_infos<Filter>(input_infos);
    if (!collected.has_value()) {
      for (auto* c : clips) {
        if (c && api.release_clip) api.release_clip(c);
      }
      return avs_new_value_error(collected.error().message.c_str());
    }

    auto init_result = [&]() -> Result<VideoFilterInstance<Filter>> {
      if constexpr (bridge_has_descriptor<Bridge>::value) {
        auto params = read_params(args, Bridge::descriptor());
        if (!params.has_value()) {
          return Result<VideoFilterInstance<Filter>>::failure(params.error());
        }
        return init_video_filter_instance<Filter>(
          collected.value(),
          &params.value(),
          HostGlobalLockCallbacks{},
          HostVariableCallbacks{},
          HostKind::AviSynth
        );
      } else {
        return init_video_filter_instance<Filter>(
          collected.value(),
          nullptr,
          HostGlobalLockCallbacks{},
          HostVariableCallbacks{},
          HostKind::AviSynth
        );
      }
    }();

    if (!init_result.has_value()) {
      for (auto* c : clips) {
        if (c && api.release_clip) api.release_clip(c);
      }
      return avs_new_value_error(init_result.error().message.c_str());
    }

    const VideoOutputInfo& output = init_result.value().output;
    if (pixel_type(output.format) == AVS_CS_UNKNOWN) {
      for (auto* c : clips) {
        if (c && api.release_clip) api.release_clip(c);
      }
      return avs_new_value_error("DualSynth C: unsupported AviSynth output format");
    }

    if (!output_origin_matches(filter_output_origin<Filter>(), output, input_infos)) {
      for (auto* c : clips) {
        if (c && api.release_clip) api.release_clip(c);
      }
      return avs_new_value_error("DualSynth C: output origin is incompatible with output video info");
    }

    if (!api.new_c_filter) {
      for (auto* c : clips) {
        if (c && api.release_clip) api.release_clip(c);
      }
      return avs_new_value_error("DualSynth C: avs_new_c_filter API not found");
    }

    AVS_FilterInfo* fi = nullptr;
    AVS_Clip* c_filter = api.new_c_filter(env, &fi, avs_void, 0);
    if (!c_filter || !fi) {
      for (auto* c : clips) {
        if (c && api.release_clip) api.release_clip(c);
      }
      return avs_new_value_error("DualSynth C: failed to create C filter");
    }

    auto* holder = new CVideoFilterStateHolder<Bridge>{
      std::move(clips),
      input_infos,
      make_video_info(output),
      output.format,
      std::move(init_result.value().state),
      bridge_mt_mode<Bridge>(),
      Bridge::parity_source_index,
      Bridge::forward_audio,
      env
    };

    fi->user_data = holder;
    fi->vi = holder->vi;
    if (holder->forward_audio && holder->clips[0] && api.get_video_info) {
      const AVS_VideoInfo* src_vi = api.get_video_info(holder->clips[0]);
      if (src_vi) {
        fi->vi.audio_samples_per_second = src_vi->audio_samples_per_second;
        fi->vi.sample_type = src_vi->sample_type;
        fi->vi.num_audio_samples = src_vi->num_audio_samples;
        fi->vi.nchannels = src_vi->nchannels;
        holder->vi = fi->vi;
      }
    }

    fi->get_frame = c_filter_get_frame<Bridge>;
    fi->get_parity = c_filter_get_parity<Bridge>;
    fi->get_audio = c_filter_get_audio<Bridge>;
    fi->set_cache_hints = c_filter_set_cache_hints<Bridge>;
    fi->free_filter = c_filter_free<Bridge>;

    AVS_Value result_val;
    if (api.set_to_clip) {
      api.set_to_clip(&result_val, c_filter);
    } else {
      result_val.type = 'c';
      result_val.array_size = 1;
      result_val.d.clip = c_filter;
    }
    if (api.release_clip) {
      api.release_clip(c_filter);
    }
    return result_val;
  } catch (const std::exception& error) {
    return avs_new_value_error(error.what());
  } catch (...) {
    return avs_new_value_error("DualSynth C: unhandled exception in filter creation");
  }
}

template <class Bridge>
inline void register_video_filter(AVS_ScriptEnvironment* env) {
  auto& api = CApi::instance();
  if (api.add_function) {
    api.add_function(
      env,
      Bridge::avs_name,
      bridge_avs_signature<Bridge>(),
      create_video_filter_bridge<Bridge>,
      nullptr
    );
  }
}

} // namespace ds::avisynth::c
