#pragma once

#include <dualsynth/span2d.hpp>
#include <dualsynth/video_bridge.hpp>
#include <dualsynth/video_filter.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

namespace ds::reference {

template <class T>
void copy_plane(span2d::Plane<const T> src, span2d::Plane<T> dst) {
  for (int y = 0; y < src.height(); ++y) {
    auto src_row = src.row(y);
    auto dst_row = dst.row(y);
    for (std::size_t x = 0; x < src_row.size(); ++x) {
      dst_row[x] = src_row[x];
    }
  }
}

template <class T>
constexpr T max_sample_value() {
  if constexpr (std::is_same_v<T, std::uint8_t>) {
    return 255;
  } else if constexpr (std::is_same_v<T, std::uint16_t>) {
    return 65535;
  } else {
    return T{1};
  }
}

template <class T>
void invert_plane(span2d::Plane<const T> src, span2d::Plane<T> dst) {
  const T max_value = max_sample_value<T>();
  for (int y = 0; y < src.height(); ++y) {
    auto src_row = src.row(y);
    auto dst_row = dst.row(y);
    for (std::size_t x = 0; x < src_row.size(); ++x) {
      dst_row[x] = static_cast<T>(max_value - src_row[x]);
    }
  }
}

template <class T>
void transpose_plane(span2d::Plane<const T> src, span2d::Plane<T> dst) {
  for (int y = 0; y < src.height(); ++y) {
    for (int x = 0; x < src.width(); ++x) {
      dst(x, y) = src(y, x);
    }
  }
}

inline Result<VideoInitResult> init_single_input_same_size(VideoInitContext& context, const char* name) {
  if (context.inputs.size() != 1) {
    return Result<VideoInitResult>::failure(
      Error{ErrorCode::InvalidArgument, std::string(name) + " requires exactly one video input"}
    );
  }

  const VideoInputInfo& input = context.inputs[0];
  return Result<VideoInitResult>::success(
    VideoInitResult{VideoOutputInfo{input.width, input.height, input.num_frames, input.format, input.fps}}
  );
}

inline Result<VideoInitResult> init_single_input_transposed(VideoInitContext& context, const char* name) {
  if (context.inputs.size() != 1) {
    return Result<VideoInitResult>::failure(
      Error{ErrorCode::InvalidArgument, std::string(name) + " requires exactly one video input"}
    );
  }

  const VideoInputInfo& input = context.inputs[0];
  return Result<VideoInitResult>::success(
    VideoInitResult{VideoOutputInfo{input.height, input.width, input.num_frames, input.format, input.fps}}
  );
}

inline Result<VideoRequestResult> request_current_frame(VideoRequestContext& context) {
  context.request_frame(0, context.output_frame);
  return Result<VideoRequestResult>::success(VideoRequestResult{});
}

inline Result<RequestedVideoFrame> get_current_input_frame(VideoProcessContext& context) {
  return context.frames.get(0, context.output_frame);
}

inline bool dimensions_match(span2d::Plane<const std::uint8_t> src, span2d::Plane<std::uint8_t> dst) {
  return src.width() == dst.width() && src.height() == dst.height();
}

inline bool transposed_dimensions_match(span2d::Plane<const std::uint8_t> src, span2d::Plane<std::uint8_t> dst) {
  return src.width() == dst.height() && src.height() == dst.width();
}

struct VideoIdentity {
  static constexpr const char* name = "VideoIdentity";
  static constexpr int input_count = 1;
  static constexpr OutputOrigin output_origin = OutputOrigin::fresh();

  static Result<VideoInitResult> init(VideoInitContext& context) {
    return init_single_input_same_size(context, name);
  }

  static Result<VideoRequestResult> request(VideoRequestContext& context) {
    return request_current_frame(context);
  }

  static Result<VideoProcessResult> process(VideoProcessContext& context) {
    auto frame = get_current_input_frame(context);
    if (!frame.has_value()) {
      return Result<VideoProcessResult>::failure(frame.error());
    }
    const auto src = as_plane<std::uint8_t>(frame.value().frame.plane(0));
    const auto dst = as_plane<std::uint8_t>(context.dst.plane(0));
    if (!dimensions_match(src, dst)) {
      return Result<VideoProcessResult>::failure(
        Error{ErrorCode::InvalidArgument, "VideoIdentity frame dimensions do not match output"}
      );
    }

    copy_plane(src, dst);
    return Result<VideoProcessResult>::success(VideoProcessResult{});
  }
};

struct VideoInvert {
  static constexpr const char* name = "VideoInvert";
  static constexpr int input_count = 1;
  static constexpr OutputOrigin output_origin = OutputOrigin::fresh();

  static Result<VideoInitResult> init(VideoInitContext& context) {
    return init_single_input_same_size(context, name);
  }

  static Result<VideoRequestResult> request(VideoRequestContext& context) {
    return request_current_frame(context);
  }

  static Result<VideoProcessResult> process(VideoProcessContext& context) {
    auto frame = get_current_input_frame(context);
    if (!frame.has_value()) {
      return Result<VideoProcessResult>::failure(frame.error());
    }
    const auto src = as_plane<std::uint8_t>(frame.value().frame.plane(0));
    const auto dst = as_plane<std::uint8_t>(context.dst.plane(0));
    if (!dimensions_match(src, dst)) {
      return Result<VideoProcessResult>::failure(
        Error{ErrorCode::InvalidArgument, "VideoInvert frame dimensions do not match output"}
      );
    }

    invert_plane(src, dst);
    return Result<VideoProcessResult>::success(VideoProcessResult{});
  }
};

struct VideoTranspose {
  static constexpr const char* name = "VideoTranspose";
  static constexpr int input_count = 1;
  static constexpr OutputOrigin output_origin = OutputOrigin::fresh();

  static Result<VideoInitResult> init(VideoInitContext& context) {
    return init_single_input_transposed(context, name);
  }

  static Result<VideoRequestResult> request(VideoRequestContext& context) {
    return request_current_frame(context);
  }

  static Result<VideoProcessResult> process(VideoProcessContext& context) {
    auto frame = get_current_input_frame(context);
    if (!frame.has_value()) {
      return Result<VideoProcessResult>::failure(frame.error());
    }
    const auto src = as_plane<std::uint8_t>(frame.value().frame.plane(0));
    const auto dst = as_plane<std::uint8_t>(context.dst.plane(0));
    if (!transposed_dimensions_match(src, dst)) {
      return Result<VideoProcessResult>::failure(
        Error{ErrorCode::InvalidArgument, "VideoTranspose frame dimensions do not match output"}
      );
    }

    transpose_plane(src, dst);
    return Result<VideoProcessResult>::success(VideoProcessResult{});
  }
};

struct VideoIdentityBridge : ds::SingleInputVideoBridgeDefaults<VideoIdentity> {
  static constexpr const char* vs_name = "VideoIdentity";
  static constexpr const char* avs_name = "DSVideoIdentity";
  static constexpr const char* missing_input_error = "DualSynth reference: missing required video clip";
  static constexpr const char* vs_format_error =
    "DualSynth reference: only GRAY8 is supported by this VS reference filter";
  static constexpr const char* avs_format_error =
    "DualSynth reference: DSVideoIdentity supports only Y8 video";
};

struct VideoInvertBridge : ds::SingleInputVideoBridgeDefaults<VideoInvert> {
  static constexpr const char* vs_name = "VideoInvert";
  static constexpr const char* avs_name = "DSVideoInvert";
  static constexpr const char* missing_input_error = "DualSynth reference: missing required video clip";
  static constexpr const char* vs_format_error =
    "DualSynth reference: only GRAY8 is supported by this VS reference filter";
  static constexpr const char* avs_format_error =
    "DualSynth reference: DSVideoInvert supports only Y8 video";
};

struct VideoTransposeBridge : ds::SingleInputVideoBridgeDefaults<VideoTranspose> {
  static constexpr const char* vs_name = "VideoTranspose";
  static constexpr const char* avs_name = "DSVideoTranspose";
  static constexpr const char* missing_input_error = "DualSynth reference: missing required video clip";
  static constexpr const char* vs_format_error =
    "DualSynth reference: only GRAY8 is supported by this VS reference filter";
  static constexpr const char* avs_format_error =
    "DualSynth reference: DSVideoTranspose supports only Y8 video";
};

} // namespace ds::reference
