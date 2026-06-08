#pragma once

#include <avisynth.h>

#include <dualsynth/format.hpp>
#include <dualsynth/frame.hpp>
#include <dualsynth/video_filter.hpp>

#include <array>
#include <cstddef>

namespace ds::avisynth {

inline int plane_id(VideoFormat format, int plane) {
  if (format.color_family == ColorFamily::Rgb) {
    static constexpr std::array<int, 4> rgb_planes{PLANAR_R, PLANAR_G, PLANAR_B, PLANAR_A};
    return rgb_planes[static_cast<std::size_t>(plane)];
  }

  static constexpr std::array<int, 4> yuv_planes{PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A};
  return yuv_planes[static_cast<std::size_t>(plane)];
}

inline int pixel_type(VideoFormat format) {
  if (format.color_family == ColorFamily::Gray) {
    switch (format.sample_format) {
    case SampleFormat::UInt8:
      return VideoInfo::CS_Y8;
    case SampleFormat::UInt16:
      return VideoInfo::CS_Y16;
    case SampleFormat::Float32:
      return VideoInfo::CS_Y32;
    case SampleFormat::UInt10:
    case SampleFormat::UInt12:
    case SampleFormat::UInt14:
      return VideoInfo::CS_UNKNOWN;
    }
  }

  if (format.color_family == ColorFamily::Rgb && format.plane_count == 3) {
    switch (format.sample_format) {
    case SampleFormat::UInt8:
      return VideoInfo::CS_RGBP;
    case SampleFormat::UInt16:
      return VideoInfo::CS_RGBP16;
    case SampleFormat::Float32:
      return VideoInfo::CS_RGBPS;
    case SampleFormat::UInt10:
    case SampleFormat::UInt12:
    case SampleFormat::UInt14:
      return VideoInfo::CS_UNKNOWN;
    }
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
