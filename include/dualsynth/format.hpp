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
  UInt16,
  Float32,
};

struct VideoFormat {
  ColorFamily color_family;
  SampleFormat sample_format;
  int plane_count;
  int subsampling_w;
  int subsampling_h;
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
};

Result<bool> is_supported_video_format(const VideoFormat& format);

} // namespace ds
