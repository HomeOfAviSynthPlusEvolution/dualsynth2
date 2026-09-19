#pragma once

#include <dualsynth/error.hpp>
#include <dualsynth/format.hpp>
#include <dualsynth/frame.hpp>
#include <dualsynth/frame_services.hpp>
#include <dualsynth/global_lock.hpp>
#include <dualsynth/host_variable.hpp>
#include <dualsynth/param.hpp>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
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

  friend constexpr bool operator==(const FrameRate& a, const FrameRate& b) noexcept {
    return a.numerator == b.numerator && a.denominator == b.denominator;
  }
  friend constexpr bool operator!=(const FrameRate& a, const FrameRate& b) noexcept {
    return !(a == b);
  }
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

// A negative input_count opts into descriptor-driven runtime clip groups.
inline constexpr int dynamic_video_inputs = -1;
template<class Filter, class T>
using VideoInputStorage = std::conditional_t<Filter::input_count == dynamic_video_inputs,
  std::vector<T>, std::array<T, (Filter::input_count < 0 ? 0 : Filter::input_count)>>;

struct VideoInputGroup {
  std::string name;
  std::size_t first = 0;
  std::size_t count = 0;
};

struct HostRequirements {
  bool frame_services = false;
  int avisynth_interface = 0;
  int avisynth_bugfix = 0;
};
template<class Filter, class = void> struct FilterRequirements {
  static constexpr HostRequirements value{};
};
template<class Filter> struct FilterRequirements<Filter, std::void_t<decltype(Filter::host_requirements)>> {
  static constexpr HostRequirements value = Filter::host_requirements;
};

enum class VideoRequestPattern { General, StrictSpatial, NoFrameReuse };
template<class Filter, class State, class = void> struct HasRequestPattern : std::false_type {};
template<class Filter, class State> struct HasRequestPattern<Filter, State,
  std::void_t<decltype(Filter::request_pattern(0, std::declval<const State&>()))>> : std::true_type {};
template<class Filter, class State>
VideoRequestPattern video_request_pattern(int input, const State& state) {
  if constexpr (HasRequestPattern<Filter, State>::value) return Filter::request_pattern(input, state);
  return VideoRequestPattern::General; // Safe default for temporal filters.
}

class VideoFrameProvider;

struct VideoInitContext {
  Span<const VideoInputInfo> inputs;
  const ParamValues* params = nullptr;
  HostGlobalLockCallbacks host_global_locks{};
  HostVariableCallbacks host_variables{};
  HostKind host = HostKind::Unknown;
  VideoFrameProvider* frames = nullptr; // Available during init; do not retain the provider.
  FrameFactory* frame_factory = nullptr;
  Span<const VideoInputGroup> input_groups{};

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

enum class OutputPixelPolicy {
  Fresh,
  CopyFromInput,
  TakeFromInput,
};

struct OutputOrigin {
  OutputPixelPolicy pixels = OutputPixelPolicy::Fresh;
  int pixel_input_index = -1;
  int prop_input_index = -1;
  int pixel_frame = -1; // -1 maps to the output frame number.
  int prop_frame = -1;

  static constexpr OutputOrigin fresh(int prop_input = 0) noexcept {
    return OutputOrigin{OutputPixelPolicy::Fresh, -1, prop_input};
  }

  static constexpr OutputOrigin fresh_without_props() noexcept {
    return OutputOrigin{OutputPixelPolicy::Fresh, -1, -1};
  }

  static constexpr OutputOrigin take_from_input(int input_index = 0) noexcept {
    return OutputOrigin{OutputPixelPolicy::TakeFromInput, input_index, input_index};
  }

  static constexpr OutputOrigin take_from_input(int pixel_input, int prop_input) noexcept {
    return OutputOrigin{OutputPixelPolicy::TakeFromInput, pixel_input, prop_input};
  }

  static constexpr OutputOrigin copy_from_input(int input_index = 0) noexcept {
    return OutputOrigin{OutputPixelPolicy::CopyFromInput, input_index, input_index};
  }

  static constexpr OutputOrigin copy_from_input(int pixel_input, int prop_input) noexcept {
    return OutputOrigin{OutputPixelPolicy::CopyFromInput, pixel_input, prop_input};
  }
};

struct RequestedVideoFrame {
  int input_index;
  int frame_number;
  VideoFrameView frame;
  FrameRef owner{}; // Populated by bridges using frame services; may outlive the provider.
};

struct VideoFrameRequest {
  int input_index;
  int frame_number;

  friend constexpr bool operator==(const VideoFrameRequest& a, const VideoFrameRequest& b) noexcept {
    return a.input_index == b.input_index && a.frame_number == b.frame_number;
  }
  friend constexpr bool operator!=(const VideoFrameRequest& a, const VideoFrameRequest& b) noexcept {
    return !(a == b);
  }
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
  Span<const VideoInputInfo> inputs{};
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
  explicit RequestedVideoFrameProvider(Span<const RequestedVideoFrame> frames)
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
  Span<const RequestedVideoFrame> frames_;
};

inline Result<VideoRequestResult> request_output_origin_frame(
  OutputOrigin origin,
  int output_frame,
  Span<const VideoInputInfo> inputs,
  std::vector<VideoFrameRequest>& requests
) {
  VideoRequestContext context{output_frame, requests, inputs};

  if (origin.pixels != OutputPixelPolicy::Fresh && origin.pixel_input_index >= 0) {
    if (origin.pixel_input_index < 0 || static_cast<std::size_t>(origin.pixel_input_index) >= inputs.size()) {
      return Result<VideoRequestResult>::failure(
        Error{ErrorCode::InvalidArgument, "DualSynth: output origin pixel input index is out of range"}
      );
    }
    context.request_frame(origin.pixel_input_index, origin.pixel_frame < 0 ? output_frame : origin.pixel_frame);
  }

  if (origin.prop_input_index >= 0) {
    if (origin.prop_input_index < 0 || static_cast<std::size_t>(origin.prop_input_index) >= inputs.size()) {
      return Result<VideoRequestResult>::failure(
        Error{ErrorCode::InvalidArgument, "DualSynth: output origin prop input index is out of range"}
      );
    }
    context.request_frame(origin.prop_input_index, origin.prop_frame < 0 ? output_frame : origin.prop_frame);
  }

  return Result<VideoRequestResult>::success(VideoRequestResult{});
}

struct VideoProcessResult {};

struct VideoProcessContext {
  int output_frame;
  VideoFrameProvider& frames;
  MutableVideoFrameView dst;
  void* filter_state = nullptr;
  FrameFactory* frame_factory = nullptr;

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
Result<VideoInputStorage<Filter, VideoInputInfo>>
collect_video_input_infos(Span<const VideoInputInfo> inputs) {
  constexpr auto input_count = static_cast<std::size_t>(Filter::input_count);
  if (Filter::input_count != dynamic_video_inputs && inputs.size() != input_count) {
    return Result<VideoInputStorage<Filter, VideoInputInfo>>::failure(
      Error{
        ErrorCode::InvalidArgument,
        std::string(Filter::name) + " received the wrong number of video inputs"
      }
    );
  }

  VideoInputStorage<Filter, VideoInputInfo> collected{};
  if constexpr (Filter::input_count == dynamic_video_inputs) collected.resize(inputs.size());
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    collected[i] = inputs[i];
  }
  return Result<VideoInputStorage<Filter, VideoInputInfo>>::success(collected);
}

template <class Filter>
Result<VideoInitResult> init_video_filter(Span<const VideoInputInfo> inputs) {
  VideoInitContext context{inputs, nullptr, {}, {}, HostKind::Unknown};
  return Filter::init(context);
}

template <class Filter>
Result<VideoInitResult> init_video_filter(Span<const VideoInputInfo> inputs, const ParamValues& params) {
  VideoInitContext context{inputs, &params, {}, {}, HostKind::Unknown};
  return Filter::init(context);
}

template <class Filter>
Result<VideoFilterInstance<Filter>> init_video_filter_instance(
  Span<const VideoInputInfo> inputs,
  const ParamValues* params,
  HostGlobalLockCallbacks host_global_locks = {},
  HostVariableCallbacks host_variables = {},
  HostKind host = HostKind::Unknown,
  VideoFrameProvider* frames = nullptr,
  FrameFactory* frame_factory = nullptr,
  Span<const VideoInputGroup> input_groups = {}
) {
  VideoInitContext context{inputs, params, host_global_locks, host_variables, host, frames, frame_factory, input_groups};
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
  Span<const VideoInputInfo> inputs,
  HostKind host = HostKind::Unknown
) {
  return init_video_filter_instance<Filter>(inputs, nullptr, {}, {}, host);
}

template <class Filter>
Result<VideoFilterInstance<Filter>> init_video_filter_instance(
  Span<const VideoInputInfo> inputs,
  const ParamValues& params,
  HostKind host = HostKind::Unknown
) {
  return init_video_filter_instance<Filter>(inputs, &params, {}, {}, host);
}

template <class Filter>
Result<VideoRequestResult> request_video_filter(
  int output_frame,
  Span<const VideoInputInfo> inputs,
  std::vector<VideoFrameRequest>& requests,
  const VideoFilterState<Filter>* state = nullptr
) {
  VideoRequestContext context{output_frame, requests, inputs, state};
  return Filter::request(context);
}

template <class Filter>
Result<VideoRequestResult> request_video_filter(
  int output_frame,
  Span<const VideoInputInfo> inputs,
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
  VideoFilterState<Filter>* state = nullptr,
  FrameFactory* factory = nullptr
) {
  VideoProcessContext context{output_frame, frames, dst, state, factory};
  return Filter::process(context);
}

template <class Filter>
Result<VideoProcessResult> process_video_filter(
  int output_frame,
  VideoFrameProvider& frames,
  MutableVideoFrameView dst,
  VideoFilterState<Filter>& state,
  FrameFactory* factory = nullptr
) {
  return process_video_filter<Filter>(output_frame, frames, dst, &state, factory);
}

template<class Filter, class = void> struct HasMappedOutputOrigin : std::false_type {};
template<class Filter> struct HasMappedOutputOrigin<Filter,
  std::void_t<decltype(Filter::output_origin_for(0, std::declval<const VideoFilterState<Filter>&>()))>> : std::true_type {};
template<class Filter, class = void> struct HasStaticOutputOrigin : std::false_type {};
template<class Filter> struct HasStaticOutputOrigin<Filter, std::void_t<decltype(Filter::output_origin)>> : std::true_type {};
template<class Filter>
OutputOrigin resolve_output_origin(int n, const VideoFilterState<Filter>& state) {
  if constexpr (HasMappedOutputOrigin<Filter>::value) return Filter::output_origin_for(n, state);
  else if constexpr (HasStaticOutputOrigin<Filter>::value) return Filter::output_origin;
  else return OutputOrigin::fresh();
}

template <class Filter, class Context, class = void>
struct filter_has_cache_hints : std::false_type {};

template <class Filter, class Context>
struct filter_has_cache_hints<
  Filter,
  Context,
  std::void_t<decltype(Filter::cache_hints(std::declval<Context&>()))>
> : std::true_type {};

template <class Filter>
int cache_hints_video_filter(
  int cachehints,
  int frame_range,
  int default_response,
  VideoFilterState<Filter>* state = nullptr
) {
  if constexpr (filter_has_cache_hints<Filter, VideoCacheHintsContext>::value) {
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
