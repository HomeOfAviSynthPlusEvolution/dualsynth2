#pragma once

#include <dualsynth/error.hpp>
#include <dualsynth/format.hpp>
#include <dualsynth/frame.hpp>
#include <dualsynth/global_lock.hpp>
#include <dualsynth/host_variable.hpp>
#include <dualsynth/param.hpp>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ds {

enum class HostKind {
  Unknown,
  VapourSynth,
  AviSynth
};

struct FrameRate {
  std::int64_t numerator = 0;
  std::int64_t denominator = 1;

  friend constexpr bool operator==(const FrameRate&, const FrameRate&) = default;
};

struct VideoInputInfo {
  int width;
  int height;
  int num_frames;
  VideoFormat format{ColorFamily::Gray, SampleFormat::UInt8, 1, 0, 0};
  FrameRate fps{};
};

struct VideoOutputInfo {
  int width;
  int height;
  int num_frames;
  VideoFormat format{ColorFamily::Gray, SampleFormat::UInt8, 1, 0, 0};
  FrameRate fps{};
};

struct VideoInitContext {
  std::span<const VideoInputInfo> inputs;
  const ParamValues* params = nullptr;
  HostGlobalLockCallbacks host_global_locks{};
  HostVariableCallbacks host_variables{};
  HostKind host = HostKind::Unknown;

  Result<bool> set_host_var(std::string_view name, ParamValue value) const {
    return set_host_variable(host_variables, name, std::move(value));
  }
};

struct VideoInitResult {
  VideoOutputInfo output;
};

template <class State>
struct VideoInitStateResult {
  VideoOutputInfo output;
  State state;
};

struct StatelessVideoFilterState {};

template <class Filter, class = void>
struct VideoFilterStateTraits {
  using type = StatelessVideoFilterState;
  static constexpr bool stateful = false;
};

template <class Filter>
struct VideoFilterStateTraits<Filter, std::void_t<typename Filter::State>> {
  using type = typename Filter::State;
  static constexpr bool stateful = true;
};

template <class Filter>
using VideoFilterState = typename VideoFilterStateTraits<Filter>::type;

template <class Filter>
struct VideoFilterInstance {
  VideoOutputInfo output;
  VideoFilterState<Filter> state;
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
  VideoFrameView frame;
};

struct VideoFrameRequest {
  int input_index;
  int frame_number;

  friend constexpr bool operator==(const VideoFrameRequest&, const VideoFrameRequest&) = default;
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
  std::span<const VideoInputInfo> inputs{};
  const void* filter_state = nullptr;

  void request_frame(int input_index, int frame_number) {
    const VideoFrameRequest request{input_index, frame_number};
    if (std::find(requests.begin(), requests.end(), request) == requests.end()) {
      requests.push_back(request);
    }
  }

  void request_frame_clamped(int input_index, int frame_number) {
    if (input_index < 0 || static_cast<std::size_t>(input_index) >= inputs.size()) {
      request_frame(input_index, frame_number);
      return;
    }

    const int num_frames = inputs[static_cast<std::size_t>(input_index)].num_frames;
    if (num_frames <= 0) {
      request_frame(input_index, 0);
      return;
    }
    if (frame_number < 0) {
      request_frame(input_index, 0);
      return;
    }
    if (frame_number >= num_frames) {
      request_frame(input_index, num_frames - 1);
      return;
    }
    request_frame(input_index, frame_number);
  }

  template <class State>
  const State& state() const {
    if (!filter_state) {
      throw std::logic_error("DualSynth: video filter state is not available");
    }
    return *static_cast<const State*>(filter_state);
  }
};

class RequestedVideoFrameProvider final : public VideoFrameProvider {
public:
  explicit RequestedVideoFrameProvider(std::span<const RequestedVideoFrame> frames)
    : frames_(frames) {}

  Result<RequestedVideoFrame> get(int input_index, int frame_number) override {
    for (const auto& frame : frames_) {
      if (frame.input_index == input_index && frame.frame_number == frame_number) {
        return Result<RequestedVideoFrame>::success(frame);
      }
    }

    return Result<RequestedVideoFrame>::failure(
      Error{ErrorCode::InvalidArgument, "DualSynth: requested video frame was not provided"}
    );
  }

private:
  std::span<const RequestedVideoFrame> frames_;
};

inline Result<VideoRequestResult> request_output_origin_frame(
  OutputOrigin origin,
  int output_frame,
  std::span<const VideoInputInfo> inputs,
  std::vector<VideoFrameRequest>& requests
) {
  if (origin.kind == OutputOriginKind::Fresh) {
    return Result<VideoRequestResult>::success(VideoRequestResult{});
  }

  if (origin.input_index < 0 || static_cast<std::size_t>(origin.input_index) >= inputs.size()) {
    return Result<VideoRequestResult>::failure(
      Error{ErrorCode::InvalidArgument, "DualSynth: output origin input index is out of range"}
    );
  }

  VideoRequestContext context{output_frame, requests, inputs};
  context.request_frame(origin.input_index, output_frame);
  return Result<VideoRequestResult>::success(VideoRequestResult{});
}

struct VideoProcessResult {};

struct VideoProcessContext {
  int output_frame;
  VideoFrameProvider& frames;
  MutableVideoFrameView dst;
  void* filter_state = nullptr;

  template <class State>
  State& state() const {
    if (!filter_state) {
      throw std::logic_error("DualSynth: video filter state is not available");
    }
    return *static_cast<State*>(filter_state);
  }
};

struct VideoCacheHintsContext {
  int cachehints;
  int frame_range;
  int default_response = 0;
  void* filter_state = nullptr;

  template <class State>
  State& state() const {
    if (!filter_state) {
      throw std::logic_error("DualSynth: video filter state is not available");
    }
    return *static_cast<State*>(filter_state);
  }
};

template <class Filter>
Result<std::array<VideoInputInfo, static_cast<std::size_t>(Filter::input_count)>>
collect_video_input_infos(std::span<const VideoInputInfo> inputs) {
  constexpr auto input_count = static_cast<std::size_t>(Filter::input_count);
  if (inputs.size() != input_count) {
    return Result<std::array<VideoInputInfo, input_count>>::failure(
      Error{
        ErrorCode::InvalidArgument,
        std::string(Filter::name) + " received the wrong number of video inputs"
      }
    );
  }

  std::array<VideoInputInfo, input_count> collected{};
  for (std::size_t i = 0; i < input_count; ++i) {
    collected[i] = inputs[i];
  }
  return Result<std::array<VideoInputInfo, input_count>>::success(collected);
}

template <class Filter>
Result<VideoInitResult> init_video_filter(std::span<const VideoInputInfo> inputs) {
  VideoInitContext context{inputs, nullptr, {}, {}, HostKind::Unknown};
  return Filter::init(context);
}

template <class Filter>
Result<VideoInitResult> init_video_filter(std::span<const VideoInputInfo> inputs, const ParamValues& params) {
  VideoInitContext context{inputs, &params, {}, {}, HostKind::Unknown};
  return Filter::init(context);
}

template <class Filter>
Result<VideoFilterInstance<Filter>> init_video_filter_instance(
  std::span<const VideoInputInfo> inputs,
  const ParamValues* params,
  HostGlobalLockCallbacks host_global_locks = {},
  HostVariableCallbacks host_variables = {},
  HostKind host = HostKind::Unknown
) {
  VideoInitContext context{inputs, params, host_global_locks, host_variables, host};
  if constexpr (VideoFilterStateTraits<Filter>::stateful) {
    auto initialized = Filter::init(context);
    if (!initialized.has_value()) {
      return Result<VideoFilterInstance<Filter>>::failure(initialized.error());
    }
    return Result<VideoFilterInstance<Filter>>::success(
      VideoFilterInstance<Filter>{
        initialized.value().output,
        std::move(initialized.value().state)
      }
    );
  } else {
    auto initialized = Filter::init(context);
    if (!initialized.has_value()) {
      return Result<VideoFilterInstance<Filter>>::failure(initialized.error());
    }
    return Result<VideoFilterInstance<Filter>>::success(
      VideoFilterInstance<Filter>{initialized.value().output, StatelessVideoFilterState{}}
    );
  }
}

template <class Filter>
Result<VideoFilterInstance<Filter>> init_video_filter_instance(
  std::span<const VideoInputInfo> inputs,
  HostKind host = HostKind::Unknown
) {
  return init_video_filter_instance<Filter>(inputs, nullptr, {}, {}, host);
}

template <class Filter>
Result<VideoFilterInstance<Filter>> init_video_filter_instance(
  std::span<const VideoInputInfo> inputs,
  const ParamValues& params,
  HostKind host = HostKind::Unknown
) {
  return init_video_filter_instance<Filter>(inputs, &params, {}, {}, host);
}

template <class Filter>
Result<VideoRequestResult> request_video_filter(
  int output_frame,
  std::span<const VideoInputInfo> inputs,
  std::vector<VideoFrameRequest>& requests,
  const VideoFilterState<Filter>* state = nullptr
) {
  VideoRequestContext context{output_frame, requests, inputs, state};
  return Filter::request(context);
}

template <class Filter>
Result<VideoRequestResult> request_video_filter(
  int output_frame,
  std::span<const VideoInputInfo> inputs,
  std::vector<VideoFrameRequest>& requests,
  const VideoFilterState<Filter>& state
) {
  return request_video_filter<Filter>(output_frame, inputs, requests, &state);
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
  MutableVideoFrameView dst,
  VideoFilterState<Filter>* state = nullptr
) {
  VideoProcessContext context{output_frame, frames, dst, state};
  return Filter::process(context);
}

template <class Filter>
Result<VideoProcessResult> process_video_filter(
  int output_frame,
  VideoFrameProvider& frames,
  MutableVideoFrameView dst,
  VideoFilterState<Filter>& state
) {
  return process_video_filter<Filter>(output_frame, frames, dst, &state);
}

template <class Filter>
int cache_hints_video_filter(
  int cachehints,
  int frame_range,
  int default_response,
  VideoFilterState<Filter>* state = nullptr
) {
  if constexpr (requires(VideoCacheHintsContext& context) { Filter::cache_hints(context); }) {
    VideoCacheHintsContext context{cachehints, frame_range, default_response, state};
    return Filter::cache_hints(context);
  } else {
    return default_response;
  }
}

template <class Filter>
int cache_hints_video_filter(
  int cachehints,
  int frame_range,
  int default_response,
  VideoFilterState<Filter>& state
) {
  return cache_hints_video_filter<Filter>(cachehints, frame_range, default_response, &state);
}

} // namespace ds
