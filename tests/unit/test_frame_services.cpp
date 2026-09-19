#include <catch2/catch_test_macros.hpp>
#include <dualsynth/frame_services.hpp>
#include <dualsynth/video_bridge.hpp>

namespace {
struct CountingFrame final : ds::FrameStorage {
  explicit CountingFrame(int& live) : live(live) { ++live; }
  ~CountingFrame() override { --live; }
  ds::VideoFrameView read() const override { return {}; }
  ds::MutableVideoFrameView write() override { return {}; }
  int& live;
};
struct DynamicCore {
  static constexpr const char* name = "Dynamic";
  static constexpr int input_count = ds::dynamic_video_inputs;
};
struct DynamicBridge {
  using Core = DynamicCore;
  static ds::FilterDescriptor descriptor() {
    return {"Dynamic",{{"count",ds::ParamType::Integer,0,false},
      {"inputs",ds::ParamType::Clip,{},true,true},
      {"optional",ds::ParamType::Clip,{},false},
      {"vs_only",ds::ParamType::Integer,0,false,false,true,false},
      {"last",ds::ParamType::Clip,{},true}}};
  }
};
}

TEST_CASE("Publishing an auxiliary frame transfers ownership and prevents subsequent writes", "[frame_services]") {
  int live = 0;
  ds::FrameRef retained;
  {
    ds::WritableFrame frame(std::make_unique<CountingFrame>(live));
    CHECK(live == 1);
    auto published = std::move(frame).publish();
    CHECK_THROWS_AS(frame.view(), std::logic_error);
    CHECK_THROWS_AS(std::move(frame).publish(), std::logic_error);
    retained = published;
  }
  CHECK(live == 1);
  CHECK_NOTHROW(retained.view());
  retained = {};
  CHECK(live == 0);
  CHECK_THROWS_AS(retained.view(),std::logic_error);
}

TEST_CASE("Dynamic input layouts preserve descriptor and host argument order", "[frame_services]") {
  auto avs = ds::bridge_clip_inputs<DynamicBridge>(false);
  auto vs = ds::bridge_clip_inputs<DynamicBridge>(true);
  REQUIRE(avs.size() == 3);
  CHECK(avs[0].array);
  CHECK(avs[0].argument == 1);
  CHECK(avs[1].optional);
  CHECK(avs[2].argument == 3);
  CHECK(vs[2].argument == 4);
  auto signature = ds::make_avisynth_signature(DynamicBridge::descriptor());
  REQUIRE(signature.has_value());
  CHECK(signature.value() == "[count]i.[optional]cc");
  std::vector<ds::VideoInputInfo> inputs(7);
  auto collected = ds::collect_video_input_infos<DynamicCore>(inputs);
  REQUIRE(collected.has_value());
  CHECK(collected.value().size() == 7);
}

TEST_CASE("Mapped output origins request only the selected input frames", "[frame_services]") {
  auto origin = ds::OutputOrigin::copy_from_input(0,1);
  origin.pixel_frame = 2;
  origin.prop_frame = 3;
  std::vector<ds::VideoInputInfo> inputs{{16,16,5},{16,16,5}};
  std::vector<ds::VideoFrameRequest> requests;
  REQUIRE(ds::request_output_origin_frame(origin,9,inputs,requests).has_value());
  REQUIRE(requests.size() == 2);
  CHECK(requests[0] == ds::VideoFrameRequest{0,2});
  CHECK(requests[1] == ds::VideoFrameRequest{1,3});
  CHECK(ds::video_request_pattern<DynamicCore>(0,ds::StatelessVideoFilterState{}) == ds::VideoRequestPattern::General);
}

TEST_CASE("Auxiliary allocation rejects invalid dimensions without host calls", "[frame_services]") {
  ds::VideoFormat gray{ds::ColorFamily::Gray,ds::SampleFormat::UInt16,1,0,0};
  CHECK_NOTHROW(ds::validate_frame_dimensions(gray,7,3));
  CHECK_THROWS_AS(ds::validate_frame_dimensions(gray,0,3),std::invalid_argument);
  CHECK_THROWS_AS(ds::validate_frame_dimensions(gray,INT_MAX,3),std::invalid_argument);
  ds::VideoFormat yuv{ds::ColorFamily::Yuv,ds::SampleFormat::UInt8,3,1,1};
  CHECK_THROWS_AS(ds::validate_frame_dimensions(yuv,7,3),std::invalid_argument);
}
