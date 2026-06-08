#pragma once

#include <avisynth.h>

#include <dualsynth/format.hpp>
#include <dualsynth/frame.hpp>
#include <dualsynth/video_filter.hpp>

#include <array>
#include <cstddef>

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

inline int plane_id(VideoFormat format, int plane) {
  if (format.color_family == ColorFamily::Rgb) {
    static constexpr std::array<int, 4> rgb_planes{PLANAR_R, PLANAR_G, PLANAR_B, PLANAR_A};
    return rgb_planes[static_cast<std::size_t>(plane)];
  }

  static constexpr std::array<int, 4> yuv_planes{PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A};
  return yuv_planes[static_cast<std::size_t>(plane)];
}

inline int yuv_pixel_type(VideoFormat format) {
  if (format.plane_count != 3) {
    return VideoInfo::CS_UNKNOWN;
  }

  if (format.subsampling_w == 0 && format.subsampling_h == 0) {
    switch (format.sample_format) {
    case SampleFormat::UInt8:
      return VideoInfo::CS_YV24;
    case SampleFormat::UInt10:
      return VideoInfo::CS_YUV444P10;
    case SampleFormat::UInt12:
      return VideoInfo::CS_YUV444P12;
    case SampleFormat::UInt14:
      return VideoInfo::CS_YUV444P14;
    case SampleFormat::UInt16:
      return VideoInfo::CS_YUV444P16;
    case SampleFormat::Float32:
      return VideoInfo::CS_YUV444PS;
    }
  }

  if (format.subsampling_w == 1 && format.subsampling_h == 0) {
    switch (format.sample_format) {
    case SampleFormat::UInt8:
      return VideoInfo::CS_YV16;
    case SampleFormat::UInt10:
      return VideoInfo::CS_YUV422P10;
    case SampleFormat::UInt12:
      return VideoInfo::CS_YUV422P12;
    case SampleFormat::UInt14:
      return VideoInfo::CS_YUV422P14;
    case SampleFormat::UInt16:
      return VideoInfo::CS_YUV422P16;
    case SampleFormat::Float32:
      return VideoInfo::CS_YUV422PS;
    }
  }

  if (format.subsampling_w == 1 && format.subsampling_h == 1) {
    switch (format.sample_format) {
    case SampleFormat::UInt8:
      return VideoInfo::CS_YV12;
    case SampleFormat::UInt10:
      return VideoInfo::CS_YUV420P10;
    case SampleFormat::UInt12:
      return VideoInfo::CS_YUV420P12;
    case SampleFormat::UInt14:
      return VideoInfo::CS_YUV420P14;
    case SampleFormat::UInt16:
      return VideoInfo::CS_YUV420P16;
    case SampleFormat::Float32:
      return VideoInfo::CS_YUV420PS;
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

  if (format.color_family == ColorFamily::Yuv) {
    return yuv_pixel_type(format);
  }

  return VideoInfo::CS_UNKNOWN;
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

} // namespace ds::avisynth
