#pragma once

#include <dualsynth/error.hpp>
#include <dualsynth/plane_span.hpp>

#include <span>
#include <vector>

namespace ds {

struct VideoInputInfo {
  int width;
  int height;
  int num_frames;
};

struct VideoOutputInfo {
  int width;
  int height;
  int num_frames;
};

struct VideoInitContext {
  std::span<const VideoInputInfo> inputs;
};

struct VideoInitResult {
  VideoOutputInfo output;
};

enum class OutputOriginKind {
  Fresh,
  CopyFromInput,
  TakeFromInput,
};

struct OutputOrigin {
  OutputOriginKind kind = OutputOriginKind::Fresh;
  int input_index = -1;

  static constexpr OutputOrigin fresh() {
    return OutputOrigin{OutputOriginKind::Fresh, -1};
  }

  static constexpr OutputOrigin copy_from_input(int index) {
    return OutputOrigin{OutputOriginKind::CopyFromInput, index};
  }

  // Move-like output construction contract.
  //
  // The output starts with the contents of the selected input, and the filter
  // promises it does not need that input as a separate immutable source during
  // processing. This gives hosts a reuse opportunity: AviSynth+ may call
  // MakeWritable() and mutate the returned frame when possible, while
  // VapourSynth must still materialize a writable copy because source frames
  // are immutable. This is an optimization contract, not an aliasing contract:
  // filters must not depend on dst sharing storage with the input, and wrappers
  // must preserve semantics even when reuse is impossible.
  static constexpr OutputOrigin take_from_input(int index) {
    return OutputOrigin{OutputOriginKind::TakeFromInput, index};
  }
};

struct RequestedVideoFrame {
  int input_index;
  int frame_number;
  PlaneSpan<const unsigned char> plane;
};

struct VideoFrameRequest {
  int input_index;
  int frame_number;
};

class VideoFrameProvider {
public:
  virtual ~VideoFrameProvider() = default;
  virtual Result<RequestedVideoFrame> get(int input_index, int frame_number) = 0;
};

struct VideoRequestResult {};

struct VideoRequestContext {
  int output_frame;
  std::vector<VideoFrameRequest>& requests;

  void request_frame(int input_index, int frame_number) {
    requests.push_back(VideoFrameRequest{input_index, frame_number});
  }
};

struct VideoProcessResult {};

struct VideoProcessContext {
  int output_frame;
  VideoFrameProvider& frames;
  PlaneSpan<unsigned char> dst;
};

template <class Filter>
Result<VideoInitResult> init_video_filter(std::span<const VideoInputInfo> inputs) {
  VideoInitContext context{inputs};
  return Filter::init(context);
}

template <class Filter>
Result<VideoRequestResult> request_video_filter(
  int output_frame,
  std::vector<VideoFrameRequest>& requests
) {
  VideoRequestContext context{output_frame, requests};
  return Filter::request(context);
}

template <class Filter>
Result<VideoProcessResult> process_video_filter(
  int output_frame,
  VideoFrameProvider& frames,
  PlaneSpan<unsigned char> dst
) {
  VideoProcessContext context{output_frame, frames, dst};
  return Filter::process(context);
}

} // namespace ds
