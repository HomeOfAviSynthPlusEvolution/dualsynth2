#include <catch2/catch_test_macros.hpp>

#include <dualsynth/video_filter.hpp>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace {

std::vector<ds::VideoInputInfo> sample_video_inputs() {
  return {
    ds::VideoInputInfo{
      64,
      48,
      10,
      ds::VideoFormat{ds::ColorFamily::Gray, ds::SampleFormat::UInt8, 1, 0, 0},
      ds::FrameRate{24, 1}
    }
  };
}

struct StatefulOffsetFilter {
  static constexpr const char* name = "StatefulOffset";
  static constexpr int input_count = 1;

  struct State {
    int offset = 0;
    int processed_frame = -1;
  };

  static ds::Result<ds::VideoInitStateResult<State>> init(ds::VideoInitContext& context) {
    const auto inputs = ds::collect_video_input_infos<StatefulOffsetFilter>(context.inputs);
    if (!inputs.has_value()) {
      return ds::Result<ds::VideoInitStateResult<State>>::failure(inputs.error());
    }

    int offset = 0;
    if (context.params) {
      const auto value = context.params->get_int("offset", 0);
      if (!value.has_value()) {
        return ds::Result<ds::VideoInitStateResult<State>>::failure(value.error());
      }
      offset = value.value();
    }

    const auto& input = inputs.value()[0];
    return ds::Result<ds::VideoInitStateResult<State>>::success(
      ds::VideoInitStateResult<State>{
        ds::VideoOutputInfo{input.width, input.height, input.num_frames, input.format, input.fps},
        State{offset, -1}
      }
    );
  }

  static ds::Result<ds::VideoRequestResult> request(ds::VideoRequestContext& context) {
    const auto& state = context.state<State>();
    context.request_frame_clamped(0, context.output_frame + state.offset);
    return ds::Result<ds::VideoRequestResult>::success(ds::VideoRequestResult{});
  }

  static ds::Result<ds::VideoProcessResult> process(ds::VideoProcessContext& context) {
    auto& state = context.state<State>();
    state.processed_frame = context.output_frame + state.offset;
    return ds::Result<ds::VideoProcessResult>::success(ds::VideoProcessResult{});
  }
};

struct HostVariableRecorder {
  int set_calls = 0;
  std::string name;
  ds::ParamValue value;

  static bool set(void* user, const char* name, const ds::ParamValue& value) {
    auto& self = *static_cast<HostVariableRecorder*>(user);
    ++self.set_calls;
    self.name = name;
    self.value = value;
    return true;
  }
};

struct HostVariableInitFilter {
  static constexpr const char* name = "HostVariableInit";
  static constexpr int input_count = 1;

  struct State {
    bool host_variable_set = false;
  };

  static ds::Result<ds::VideoInitStateResult<State>> init(ds::VideoInitContext& context) {
    const auto inputs = ds::collect_video_input_infos<HostVariableInitFilter>(context.inputs);
    if (!inputs.has_value()) {
      return ds::Result<ds::VideoInitStateResult<State>>::failure(inputs.error());
    }

    auto set_variable = context.set_host_var("ThirdPartyReady", ds::ParamValue{"ready"});
    if (!set_variable.has_value()) {
      return ds::Result<ds::VideoInitStateResult<State>>::failure(set_variable.error());
    }

    const auto& input = inputs.value()[0];
    return ds::Result<ds::VideoInitStateResult<State>>::success(
      ds::VideoInitStateResult<State>{
        ds::VideoOutputInfo{input.width, input.height, input.num_frames, input.format, input.fps},
        State{set_variable.value()}
      }
    );
  }
};

struct HostKindInitFilter {
  static constexpr const char* name = "HostKindInit";
  static constexpr int input_count = 1;

  struct State {
    ds::HostKind host = ds::HostKind::Unknown;
  };

  static ds::Result<ds::VideoInitStateResult<State>> init(ds::VideoInitContext& context) {
    const auto inputs = ds::collect_video_input_infos<HostKindInitFilter>(context.inputs);
    if (!inputs.has_value()) {
      return ds::Result<ds::VideoInitStateResult<State>>::failure(inputs.error());
    }

    const auto& input = inputs.value()[0];
    return ds::Result<ds::VideoInitStateResult<State>>::success(
      ds::VideoInitStateResult<State>{
        ds::VideoOutputInfo{input.width, input.height, input.num_frames, input.format, input.fps},
        State{context.host}
      }
    );
  }
};

} // namespace

TEST_CASE("stateful video filters initialize from params and reuse state in request and process") {
  const std::vector<ds::VideoInputInfo> inputs{
    ds::VideoInputInfo{
      64,
      48,
      10,
      ds::VideoFormat{ds::ColorFamily::Gray, ds::SampleFormat::UInt8, 1, 0, 0},
      ds::FrameRate{24, 1}
    }
  };
  const ds::ParamValues params{
    std::vector<ds::ParamEntry>{
      ds::ParamEntry{"offset", ds::ParamValue{2}}
    }
  };

  auto instance = ds::init_video_filter_instance<StatefulOffsetFilter>(inputs, params);

  REQUIRE(instance.has_value());
  REQUIRE(instance.value().output.width == 64);
  REQUIRE(instance.value().state.offset == 2);

  std::vector<ds::VideoFrameRequest> requests;
  const auto request = ds::request_video_filter<StatefulOffsetFilter>(
    5,
    inputs,
    requests,
    instance.value().state
  );

  REQUIRE(request.has_value());
  REQUIRE(requests == std::vector<ds::VideoFrameRequest>{ds::VideoFrameRequest{0, 7}});

  ds::RequestedVideoFrameProvider provider({});
  ds::MutableVideoFrameView dst{};
  const auto process = ds::process_video_filter<StatefulOffsetFilter>(
    5,
    provider,
    dst,
    instance.value().state
  );

  REQUIRE(process.has_value());
  REQUIRE(instance.value().state.processed_frame == 7);
}

TEST_CASE("video init context writes host variables through the host callback") {
  HostVariableRecorder recorder;
  const ds::HostVariableCallbacks callbacks{
    &recorder,
    &HostVariableRecorder::set
  };

  auto instance = ds::init_video_filter_instance<HostVariableInitFilter>(
    sample_video_inputs(),
    nullptr,
    {},
    callbacks
  );

  REQUIRE(instance.has_value());
  REQUIRE(instance.value().state.host_variable_set);
  REQUIRE(recorder.set_calls == 1);
  REQUIRE(recorder.name == "ThirdPartyReady");
  REQUIRE(std::get<std::string>(recorder.value.value) == "ready");
}

TEST_CASE("video init context treats missing host variable support as a successful no-op") {
  auto instance = ds::init_video_filter_instance<HostVariableInitFilter>(sample_video_inputs());

  REQUIRE(instance.has_value());
  REQUIRE(instance.value().state.host_variable_set);
}

TEST_CASE("video init context defaults to unknown host kind") {
  auto instance = ds::init_video_filter_instance<HostKindInitFilter>(sample_video_inputs());

  REQUIRE(instance.has_value());
  REQUIRE(instance.value().state.host == ds::HostKind::Unknown);
}

TEST_CASE("video init context carries explicit host kind") {
  auto vapoursynth_instance = ds::init_video_filter_instance<HostKindInitFilter>(
    sample_video_inputs(),
    ds::HostKind::VapourSynth
  );
  auto avisynth_instance = ds::init_video_filter_instance<HostKindInitFilter>(
    sample_video_inputs(),
    ds::HostKind::AviSynth
  );

  REQUIRE(vapoursynth_instance.has_value());
  REQUIRE(vapoursynth_instance.value().state.host == ds::HostKind::VapourSynth);
  REQUIRE(avisynth_instance.has_value());
  REQUIRE(avisynth_instance.value().state.host == ds::HostKind::AviSynth);
}
