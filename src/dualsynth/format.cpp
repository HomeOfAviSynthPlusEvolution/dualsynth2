#include <dualsynth/format.hpp>

namespace ds {

Result<bool> is_supported_video_format(const VideoFormat& format) {
  if (format.plane_count < 1 || format.plane_count > 4) {
    return Result<bool>::failure({
      ErrorCode::InvalidArgument,
      "video format plane count must be between 1 and 4"
    });
  }

  if (format.sample_format == SampleFormat::UInt10 ||
      format.sample_format == SampleFormat::UInt12 ||
      format.sample_format == SampleFormat::UInt14) {
    return Result<bool>::failure({
      ErrorCode::UnsupportedFormat,
      "logical 10/12/14-bit formats must be converted to uint16 or float32 before DualSynth"
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
  case 10:
    return Result<SampleFormat>::failure({
      ErrorCode::UnsupportedFormat,
      "logical 10-bit formats must be converted to uint16 or float32 before DualSynth"
    });
  case 12:
    return Result<SampleFormat>::failure({
      ErrorCode::UnsupportedFormat,
      "logical 12-bit formats must be converted to uint16 or float32 before DualSynth"
    });
  case 14:
    return Result<SampleFormat>::failure({
      ErrorCode::UnsupportedFormat,
      "logical 14-bit formats must be converted to uint16 or float32 before DualSynth"
    });
  case 16:
    return Result<SampleFormat>::success(SampleFormat::UInt16);
  default:
    return Result<SampleFormat>::failure({
      ErrorCode::UnsupportedFormat,
      "only 8-bit, 16-bit, and 32-bit float video samples are supported"
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
  case SampleFormat::UInt8:
    return 1;
  case SampleFormat::UInt16:
    return 2;
  case SampleFormat::Float32:
    return 4;
  case SampleFormat::UInt10:
  case SampleFormat::UInt12:
  case SampleFormat::UInt14:
    return 0;
  }
  return 0;
}

} // namespace ds
