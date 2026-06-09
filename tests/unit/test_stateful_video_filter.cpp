#include <catch2/catch_test_macros.hpp>

#include <dualsynth/video_filter.hpp>

#include <cstdint>
#include <vector>

namespace {

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
