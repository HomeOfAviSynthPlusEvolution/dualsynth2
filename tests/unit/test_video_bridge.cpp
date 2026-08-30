#include <catch2/catch_test_macros.hpp>
#include <dualsynth/param.hpp>
#include <dualsynth/video_bridge.hpp>
#include <dualsynth/video_filter.hpp>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace {

struct SampleCore {
  static constexpr int input_count = 1;
};

struct SampleBridge : ds::SingleInputVideoBridgeDefaults<SampleCore> {};

struct CompleteBridge : ds::SingleInputVideoBridgeDefaults<SampleCore> {
  static constexpr const char* vs_name = "Sample";
  static constexpr const char* avs_name = "DSSample";
  static constexpr const char* vs_format_error = "Sample requires GRAY8";
  static constexpr const char* avs_format_error = "DSSample requires Y8";
};

} // namespace

TEST_CASE("SingleInputVideoBridgeDefaults provides reusable host binding defaults") {
  REQUIRE((std::is_same_v<SampleBridge::Core, SampleCore>));
  REQUIRE(std::string(SampleBridge::vs_signature) == "clip:vnode;");
  REQUIRE(SampleBridge::vs_input_names.size() == 1);
  REQUIRE(std::string(SampleBridge::vs_input_names[0]) == "clip");
  REQUIRE(std::string(SampleBridge::avs_signature) == "c");
  REQUIRE(std::string(SampleBridge::missing_input_error) == "DualSynth: missing required video clip");
  REQUIRE(SampleBridge::parity_source_index == 0);
  REQUIRE(SampleBridge::forward_audio == true);
}

TEST_CASE("VideoBridge concept accepts complete bridge metadata") {
  REQUIRE(ds::VideoBridge<CompleteBridge>);
}

TEST_CASE("Video bridge signatures are generated from host-specific parameter metadata") {
  const ds::FilterDescriptor descriptor{
    "DFTTest",
    std::vector<ds::ParamSpec>{
      ds::ParamSpec{"clip", ds::ParamType::Clip, ds::ParamValue{}, true},
      ds::ParamSpec{"sigma", ds::ParamType::Float, ds::ParamValue{8.0}, false},
      ds::ParamSpec{
        "slocation",
        ds::ParamType::Float,
        ds::ParamValue{std::vector<double>{}},
        false,
        true
      },
      ds::ParamSpec{
        "planes",
        ds::ParamType::Integer,
        ds::ParamValue{std::vector<std::int64_t>{0, 1, 2}},
        false,
        true,
        true,
        false
      },
      ds::ParamSpec{"y", ds::ParamType::Integer, ds::ParamValue{3}, false, false, false, true},
      ds::ParamSpec{"zmean", ds::ParamType::Boolean, ds::ParamValue{false}, false},
      ds::ParamSpec{"mode", ds::ParamType::String, ds::ParamValue{"fast"}, false}
    }
  };

  const auto vs_signature = ds::make_vapoursynth_signature(descriptor);
  const auto avs_signature = ds::make_avisynth_signature(descriptor);

  REQUIRE(vs_signature.has_value());
  REQUIRE(avs_signature.has_value());
  REQUIRE(
    vs_signature.value() ==
    "clip:vnode;sigma:float:opt;slocation:float[]:opt;planes:int[]:opt;zmean:int:opt;mode:data:opt;"
  );
  REQUIRE(avs_signature.value() == "c[sigma]f[slocation]s[y]i[zmean]b[mode]s[slocation()]f");
}

TEST_CASE("Video request context deduplicates and clamps requested frames") {
  std::vector<ds::VideoFrameRequest> requests;
  const std::vector<ds::VideoInputInfo> inputs{
    ds::VideoInputInfo{16, 9, 5}
  };
  ds::VideoRequestContext context{3, requests, inputs};

  context.request_frame(0, 3);
  context.request_frame(0, 3);
  context.request_frame_clamped(0, -2);
  context.request_frame_clamped(0, 9);

  REQUIRE(requests.size() == 3);
  REQUIRE(requests[0] == ds::VideoFrameRequest{0, 3});
  REQUIRE(requests[1] == ds::VideoFrameRequest{0, 0});
  REQUIRE(requests[2] == ds::VideoFrameRequest{0, 4});
}

TEST_CASE("Requested frame provider returns a clear error for missing frames") {
  const std::uint8_t pixel = 17;
  const ds::VideoFrameView frame{
    ds::VideoFormat{ds::ColorFamily::Gray, ds::SampleFormat::UInt8, 1, 0, 0},
    1,
    std::array<ds::PlaneView, 4>{
      ds::PlaneView{&pixel, 1, 1, 1},
      ds::PlaneView{},
      ds::PlaneView{},
      ds::PlaneView{}
    }
  };
  const std::vector<ds::RequestedVideoFrame> frames{
    ds::RequestedVideoFrame{1, 4, frame}
  };
  ds::RequestedVideoFrameProvider provider{frames};

  const auto found = provider.get(1, 4);
  const auto missing = provider.get(0, 4);

  REQUIRE(found.has_value());
  REQUIRE(found.value().frame.plane(0).data == &pixel);
  REQUIRE_FALSE(missing.has_value());
  REQUIRE(missing.error().code == ds::ErrorCode::InvalidArgument);
}

TEST_CASE("Output origin requests its source frame and rejects invalid inputs") {
  const std::vector<ds::VideoInputInfo> inputs{
    ds::VideoInputInfo{16, 9, 8},
    ds::VideoInputInfo{16, 9, 8}
  };
  std::vector<ds::VideoFrameRequest> requests{
    ds::VideoFrameRequest{1, 4}
  };

  const auto copy_result = ds::request_output_origin_frame(
    ds::OutputOrigin::copy_from_input(1),
    4,
    inputs,
    requests
  );
  const auto fresh_without_props_result = ds::request_output_origin_frame(
    ds::OutputOrigin::fresh_without_props(),
    4,
    inputs,
    requests
  );
  const auto invalid_result = ds::request_output_origin_frame(
    ds::OutputOrigin::take_from_input(2),
    4,
    inputs,
    requests
  );

  REQUIRE(copy_result.has_value());
  REQUIRE(fresh_without_props_result.has_value());
  REQUIRE(requests.size() == 1);
  REQUIRE(requests[0] == ds::VideoFrameRequest{1, 4});
  REQUIRE_FALSE(invalid_result.has_value());
  REQUIRE(invalid_result.error().code == ds::ErrorCode::InvalidArgument);

  const auto fresh_with_props_result = ds::request_output_origin_frame(
    ds::OutputOrigin::fresh(0),
    4,
    inputs,
    requests
  );
  REQUIRE(fresh_with_props_result.has_value());
  REQUIRE(requests.size() == 2);
  REQUIRE(requests[1] == ds::VideoFrameRequest{0, 4});
}
