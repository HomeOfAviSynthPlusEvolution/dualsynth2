#pragma once

#include <dualsynth/video_filter.hpp>

#include <array>
#include <cstddef>
#include <string>

namespace ds::acceptance {

inline Result<VideoRequestResult> temporal_average3_request(VideoRequestContext& context) {
  context.request_frame(0, context.output_frame - 1);
  context.request_frame(1, context.output_frame);
  context.request_frame(2, context.output_frame + 1);
  return Result<VideoRequestResult>::success(VideoRequestResult{});
}

inline Result<VideoProcessResult> temporal_average3_process(VideoProcessContext& context) {
  std::array<Result<RequestedVideoFrame>, 3> frames{
    context.frames.get(0, context.output_frame - 1),
    context.frames.get(1, context.output_frame),
    context.frames.get(2, context.output_frame + 1)
  };

  for (const auto& frame : frames) {
    if (!frame.has_value()) {
      return Result<VideoProcessResult>::failure(frame.error());
    }
  }

  const auto& a = frames[0].value().plane;
  const auto& b = frames[1].value().plane;
  const auto& c = frames[2].value().plane;

  if (a.width() != context.dst.width() ||
      b.width() != context.dst.width() ||
      c.width() != context.dst.width() ||
      a.height() != context.dst.height() ||
      b.height() != context.dst.height() ||
      c.height() != context.dst.height()) {
    return Result<VideoProcessResult>::failure(
      Error{ErrorCode::InvalidArgument, "AcceptanceTemporalAverage3 frame dimensions do not match output"}
    );
  }

  for (int y = 0; y < context.dst.height(); ++y) {
    const auto a_row = a.row(y);
    const auto b_row = b.row(y);
    const auto c_row = c.row(y);
    const auto dst_row = context.dst.row(y);
    for (int x = 0; x < context.dst.width(); ++x) {
      const auto index = static_cast<std::size_t>(x);
      dst_row[index] = static_cast<unsigned char>(
        (static_cast<int>(a_row[index]) +
         static_cast<int>(b_row[index]) +
         static_cast<int>(c_row[index])) / 3
      );
    }
  }

  return Result<VideoProcessResult>::success(VideoProcessResult{});
}

} // namespace ds::acceptance
