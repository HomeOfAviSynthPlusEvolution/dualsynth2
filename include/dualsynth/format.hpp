#pragma once

#include <dualsynth/error.hpp>
#include <dualsynth/media.hpp>

namespace ds {

enum class ColorFamily {
  Gray,
  Yuv,
  Rgb,
};

enum class SampleFormat {
  UInt8,
  UInt10,
  UInt12,
  UInt14,
  UInt16,
  Float32,
};

struct VideoFormat {
  ColorFamily color_family;
  SampleFormat sample_format;
  int plane_count;
  int subsampling_w;
  int subsampling_h;

  friend constexpr bool operator==(const VideoFormat&, const VideoFormat&) = default;
};

enum class AudioSampleFormat {
  Int16,
  Int32,
  Float32,
  Float64,
};

struct AudioFormat {
  AudioSampleFormat sample_format;
  int sample_rate;
  int channels;

  friend constexpr bool operator==(const AudioFormat&, const AudioFormat&) = default;
};

Result<bool> is_supported_video_format(const VideoFormat& format);
Result<SampleFormat> sample_format_from_depth(bool floating_point, int bits_per_sample);
Result<VideoFormat> make_video_format(
  ColorFamily color_family,
  bool floating_point,
  int bits_per_sample,
  int plane_count,
  int subsampling_w,
  int subsampling_h
);
int bytes_per_sample(SampleFormat sample_format);

} // namespace ds
