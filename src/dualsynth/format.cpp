#include <dualsynth/format.hpp>

namespace ds {

Result<bool> is_supported_video_format(const VideoFormat& format) {
  if (format.plane_count < 1 || format.plane_count > 4) {
    return Result<bool>::failure({
      ErrorCode::InvalidArgument,
      "video format plane count must be between 1 and 4"
    });
  }

  return Result<bool>::success(true);
}

Result<SampleFormat> sample_format_from_depth(bool floating_point, int bits_per_sample) {
  if (floating_point) {
    if (bits_per_sample == 32) {
      return Result<SampleFormat>::success(SampleFormat::Float32);
    }
    return Result<SampleFormat>::failure({
      ErrorCode::UnsupportedFormat,
      "only 32-bit float video samples are supported"
    });
  }

  switch (bits_per_sample) {
  case 8:
    return Result<SampleFormat>::success(SampleFormat::UInt8);
  case 9:
    return Result<SampleFormat>::success(SampleFormat::UInt9);
  case 10:
    return Result<SampleFormat>::success(SampleFormat::UInt10);
  case 11:
    return Result<SampleFormat>::success(SampleFormat::UInt11);
  case 12:
    return Result<SampleFormat>::success(SampleFormat::UInt12);
  case 13:
    return Result<SampleFormat>::success(SampleFormat::UInt13);
  case 14:
    return Result<SampleFormat>::success(SampleFormat::UInt14);
  case 15:
    return Result<SampleFormat>::success(SampleFormat::UInt15);
  case 16:
    return Result<SampleFormat>::success(SampleFormat::UInt16);
  default:
    return Result<SampleFormat>::failure({
      ErrorCode::UnsupportedFormat,
      "only 8 through 16-bit integer and 32-bit float video samples are supported"
    });
  }
}

Result<VideoFormat> make_video_format(
  ColorFamily color_family,
  bool floating_point,
  int bits_per_sample,
  int plane_count,
  int subsampling_w,
  int subsampling_h
) {
  auto sample_format = sample_format_from_depth(floating_point, bits_per_sample);
  if (!sample_format.has_value()) {
    return Result<VideoFormat>::failure(sample_format.error());
  }

  VideoFormat format{
    color_family,
    sample_format.value(),
    plane_count,
    subsampling_w,
    subsampling_h
  };

  const auto supported = is_supported_video_format(format);
  if (!supported.has_value()) {
    return Result<VideoFormat>::failure(supported.error());
  }
  return Result<VideoFormat>::success(format);
}

int bytes_per_sample(SampleFormat sample_format) {
  switch (sample_format) {
  case SampleFormat::UInt8: return 1;
  case SampleFormat::UInt9:
  case SampleFormat::UInt10:
  case SampleFormat::UInt11:
  case SampleFormat::UInt12:
  case SampleFormat::UInt13:
  case SampleFormat::UInt14:
  case SampleFormat::UInt15:
  case SampleFormat::UInt16: return 2;
  case SampleFormat::Float32: return 4;
  }
  return 0;
}

int bits_per_sample(SampleFormat sample_format) {
  switch (sample_format) {
  case SampleFormat::UInt8: return 8;
  case SampleFormat::UInt9: return 9;
  case SampleFormat::UInt10: return 10;
  case SampleFormat::UInt11: return 11;
  case SampleFormat::UInt12: return 12;
  case SampleFormat::UInt13: return 13;
  case SampleFormat::UInt14: return 14;
  case SampleFormat::UInt15: return 15;
  case SampleFormat::UInt16: return 16;
  case SampleFormat::Float32: return 32;
  }
  return 0;
}

} // namespace ds
