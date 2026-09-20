#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <dualsynth/frame_services.hpp>
#include <dualsynth/video_bridge.hpp>
#include <dualsynth/staged_video.hpp>

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

TEST_CASE("Invalid auxiliary sample formats fail before dimension arithmetic", "[frame_services]") {
  ds::VideoFormat invalid{ds::ColorFamily::Gray,static_cast<ds::SampleFormat>(99),1,0,0};
  CHECK_THROWS_AS(ds::validate_frame_dimensions(invalid,16,8),std::invalid_argument);
}

namespace {
int live_requests = 0;
struct LifetimeStages {
  static constexpr ds::HostRequirements host_requirements{true,11,0};
  static constexpr ds::OutputOrigin output_origin = ds::OutputOrigin::fresh_without_props();
  struct RequestState {
    int phase = 0;
    ds::FrameRef held;
    RequestState() { ++live_requests; }
    ~RequestState() { --live_requests; }
  };
  static ds::Result<ds::VideoStageResult> advance(ds::VideoStageContext& ctx, RequestState& r) {
    if (r.phase++ == 0) {
      ctx.request_frame(0,ctx.output_frame);
      ctx.request_frame(0,ctx.output_frame);
      return ds::Result<ds::VideoStageResult>::success(ds::VideoStageResult::RequestFrames);
    }
    r.held = ctx.frames.get(0,ctx.output_frame).value().owner;
    if (ctx.output_frame == 1) throw std::runtime_error("stage failure");
    return ds::Result<ds::VideoStageResult>::success(ds::VideoStageResult::Ready);
  }
  static ds::Result<ds::VideoProcessResult> process(ds::VideoProcessContext& ctx, RequestState&) {
    if (ctx.output_frame == 2) throw std::runtime_error("process failure");
    return ds::Result<ds::VideoProcessResult>::success({});
  }
};
struct CountingProvider : ds::VideoFrameProvider {
  int& live;
  int calls = 0;
  explicit CountingProvider(int& live) : live(live) {}
  ds::Result<ds::RequestedVideoFrame> get(int input,int n) override {
    ++calls;
    ds::FrameRef owner(std::make_shared<CountingFrame>(live));
    return ds::Result<ds::RequestedVideoFrame>::success({input,n,owner.view(),owner});
  }
};
struct CountingFactory : ds::FrameFactory {
  int& live;
  explicit CountingFactory(int& live) : live(live) {}
  ds::WritableFrame allocate(ds::VideoFormat,int,int,const ds::FrameRef&) override {
    return ds::WritableFrame(std::make_unique<CountingFrame>(live));
  }
};
}
TEST_CASE("Staged request and output ownership unwind on success, stage errors and process errors", "[staged_video]") {
  ds::StatelessVideoFilterState state;
  std::vector<ds::VideoInputInfo> inputs{{16,8,8}};
  ds::VideoOutputInfo output{16,8,8};
  int live = 0;
  CountingProvider provider(live);
  CountingFactory factory(live);
  for (int n : {0,1,2}) {
    {
      ds::StagedVideoRequest<LifetimeStages> request(n,state);
      CHECK(live_requests == 1);
      if (n == 1) {
        CHECK_THROWS_WITH(ds::acquire_video_stages(request,provider,inputs,state),"stage failure");
      } else {
        ds::acquire_video_stages(request,provider,inputs,state);
        CHECK(live == 1);
        CHECK_FALSE(request.get(0,7).has_value());
        if (n == 2) CHECK_THROWS_WITH(request.finish(output,inputs,state,factory),"process failure");
        else {
          auto frame = request.finish(output,inputs,state,factory);
          CHECK(live == 2);
        }
      }
      CHECK(live == 1);
    }
    CHECK(live == 0);
    CHECK(live_requests == 0);
  }
  CHECK(provider.calls == 3); // Duplicate declarations never reach the provider.
  {
    ds::StagedVideoRequest<LifetimeStages> a(0,state), b(3,state);
    CHECK_FALSE(a.advance(inputs,state));
    CHECK_FALSE(b.advance(inputs,state));
    CHECK(live_requests == 2);
    a.accept(provider.get(0,0).value());
    b.accept(provider.get(0,3).value());
    CHECK(a.advance(inputs,state));
    CHECK(b.advance(inputs,state));
    CHECK(a.get(0,0).has_value());
    CHECK_FALSE(a.get(0,3).has_value());
  }
  CHECK(live == 0);
  CHECK(live_requests == 0);
}

namespace {
struct InvalidStages : LifetimeStages {
  static ds::VideoRequestPattern request_pattern(int,const ds::StatelessVideoFilterState&) {
    return ds::VideoRequestPattern::StrictSpatial;
  }
  static ds::Result<ds::VideoStageResult> advance(ds::VideoStageContext& ctx, RequestState&) {
    switch (ctx.output_frame) {
      case 1: ctx.request_frame(-1,0); break;
      case 2: ctx.request_frame(0,8); break;
      case 3: ctx.request_frame(0,4); break;
      case 4: ctx.request_frame(0,4); return ds::Result<ds::VideoStageResult>::success(ds::VideoStageResult::Ready);
    }
    return ds::Result<ds::VideoStageResult>::success(ds::VideoStageResult::RequestFrames);
  }
};
}
TEST_CASE("Staged requests reject invalid ranges, no progress and contradictory Ready results", "[staged_video]") {
  ds::StatelessVideoFilterState state;
  std::vector<ds::VideoInputInfo> inputs{{16,8,8}};
  const char* messages[] = {"DualSynth: stage made no new frame requests",
    "DualSynth: staged frame request is out of range", "DualSynth: staged frame request is out of range",
    "DualSynth: temporal request violates strict spatial dependency", "DualSynth: Ready stage requested more frames"};
  for (int n = 0; n < 5; ++n) {
    ds::StagedVideoRequest<InvalidStages> request(n,state);
    CHECK_THROWS_WITH(request.advance(inputs,state),messages[n]);
  }
  CHECK(live_requests == 0);
}
