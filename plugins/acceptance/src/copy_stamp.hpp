#pragma once

#include <dualsynth/video_filter.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace ds::acceptance {

struct AcceptanceCopyStamp {
  static constexpr const char* name = "AcceptanceCopyStamp";
  static constexpr int input_count = 1;
  static constexpr OutputOrigin output_origin = OutputOrigin::copy_from_input(0);

  static Result<VideoInitResult> init(VideoInitContext& context) {
    if (context.inputs.size() != input_count) {
      return Result<VideoInitResult>::failure(
        Error{ErrorCode::InvalidArgument, "AcceptanceCopyStamp requires exactly one video input"}
      );
    }

    const VideoInputInfo& input = context.inputs[0];
    if (input.format != VideoFormat{ColorFamily::Gray, SampleFormat::UInt8, 1, 0, 0}) {
      return Result<VideoInitResult>::failure(
        Error{ErrorCode::UnsupportedFormat, "AcceptanceCopyStamp requires GRAY8 video"}
      );
    }

    return Result<VideoInitResult>::success(
      VideoInitResult{VideoOutputInfo{input.width, input.height, input.num_frames, input.format, input.fps}}
    );
  }

  static Result<VideoRequestResult> request(VideoRequestContext&) {
    return Result<VideoRequestResult>::success(VideoRequestResult{});
  }

  static Result<VideoProcessResult> process(VideoProcessContext& context) {
    auto dst = as_plane<std::uint8_t>(context.dst.plane(0));
    if (dst.empty()) {
      return Result<VideoProcessResult>::failure(
        Error{ErrorCode::InvalidArgument, "AcceptanceCopyStamp received an empty output frame"}
      );
    }

    dst(0, 0) = 255;
    return Result<VideoProcessResult>::success(VideoProcessResult{});
  }
};

struct AcceptanceCopyStampBridge {
  using Core = AcceptanceCopyStamp;

  static constexpr const char* vs_name = "AcceptanceCopyStamp";
  static constexpr const char* vs_signature = "clip:vnode;";
  static constexpr std::array<const char*, static_cast<std::size_t>(Core::input_count)> vs_input_names{
    "clip"
  };

  static constexpr const char* avs_name = "DSAcceptanceCopyStamp";
  static constexpr const char* avs_signature = "c";

  static constexpr const char* missing_input_error =
    "DualSynth reference: missing required AcceptanceCopyStamp clip";
  static constexpr const char* vs_format_error =
    "DualSynth reference: AcceptanceCopyStamp supports only GRAY8 video";
  static constexpr const char* avs_format_error =
    "DualSynth reference: DSAcceptanceCopyStamp supports only Y8 video";
  static constexpr std::size_t parity_source_index = 0;
  static constexpr bool forward_audio = false;

  static constexpr bool accepts_video_format(VideoFormat format) {
    return format == VideoFormat{ColorFamily::Gray, SampleFormat::UInt8, 1, 0, 0};
  }
};

} // namespace ds::acceptance
