#include <dualsynth/format.hpp>

namespace ds {

Result<bool> is_supported_video_format(const VideoFormat& format) {
  if (format.plane_count < 1 || format.plane_count > 4) {
    return Result<bool>::failure({
      ErrorCode::InvalidArgument,
      "video format plane count must be between 1 and 4"
    });
  }

  if (format.sample_format == SampleFormat::UInt10) {
    return Result<bool>::failure({
      ErrorCode::UnsupportedFormat,
      "logical 10-bit formats must be converted to uint16 or float32 before DualSynth"
    });
  }

  return Result<bool>::success(true);
}

} // namespace ds
