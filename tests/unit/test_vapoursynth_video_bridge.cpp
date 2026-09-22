#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <dualsynth/vapoursynth/video_bridge.hpp>
#include <dualsynth/video_bridge.hpp>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace {
struct StagedNativeFrame {
  int references = 0;
  uint8_t pixel = 37;
};
struct StagedNativeContext {
  std::map<std::pair<VSNode*,int>,StagedNativeFrame*> available;
  int gets = 0;
  int releases = 0;
  int fail_get = 0;
  ~StagedNativeContext() {
    for (const auto& entry : available) --entry.second->references;
  }
};
const VSFrame* VS_CC staged_fake_get(int n, VSNode* node, VSFrameContext* ctx) noexcept {
  auto& host = *reinterpret_cast<StagedNativeContext*>(ctx);
  if (++host.gets == host.fail_get) return nullptr;
  const auto it = host.available.find({node,n});
  if (it == host.available.end()) return nullptr;
  ++it->second->references;
  return reinterpret_cast<const VSFrame*>(it->second);
}
void VS_CC staged_fake_release(VSNode* node, int n, VSFrameContext* ctx) noexcept {
  auto& host = *reinterpret_cast<StagedNativeContext*>(ctx);
  ++host.releases;
  const auto it = host.available.find({node,n});
  if (it != host.available.end()) {
    --it->second->references;
    host.available.erase(it);
  }
}
void VS_CC staged_fake_free(const VSFrame* frame) noexcept {
  --const_cast<StagedNativeFrame*>(reinterpret_cast<const StagedNativeFrame*>(frame))->references;
}
const VSVideoFormat* VS_CC staged_fake_format(const VSFrame*) noexcept {
  static const VSVideoFormat format{cfGray,stInteger,8,1,0,0,1};
  return &format;
}
const uint8_t* VS_CC staged_fake_pixels(const VSFrame* frame, int) noexcept {
  return &reinterpret_cast<const StagedNativeFrame*>(frame)->pixel;
}
int VS_CC staged_fake_size(const VSFrame*, int) noexcept { return 1; }
ptrdiff_t VS_CC staged_fake_stride(const VSFrame*, int) noexcept { return 1; }
VSAPI staged_fake_api() {
  VSAPI api{};
  api.getFrameFilter = staged_fake_get;
  api.releaseFrameEarly = staged_fake_release;
  api.freeFrame = staged_fake_free;
  api.getVideoFrameFormat = staged_fake_format;
  api.getReadPtr = staged_fake_pixels;
  api.getStride = staged_fake_stride;
  api.getFrameWidth = staged_fake_size;
  api.getFrameHeight = staged_fake_size;
  return api;
}
struct AliasedStages {
  static constexpr ds::HostRequirements host_requirements{true,0,0};
  static constexpr ds::OutputOrigin output_origin = ds::OutputOrigin::fresh_without_props();
  struct State { bool release = true; };
  struct RequestState { int step = 0; };
  static ds::Result<ds::VideoStageResult> advance(ds::VideoStageContext& ctx, RequestState& r) {
    if (r.step++ == 0) {
      ctx.request_frame(0,0);
      ctx.request_frame(1,0);
      return ds::Result<ds::VideoStageResult>::success(ds::VideoStageResult::RequestFrames);
    }
    CHECK(*static_cast<const uint8_t*>(ctx.frames.get(0,0).value().frame.plane(0).data) == 37);
    CHECK(ctx.frames.get(1,0).has_value());
    if (r.step == 2 && ctx.state<State>().release) {
      REQUIRE(ctx.release_frame(0,0).has_value());
      ctx.request_frame(0,0);
      return ds::Result<ds::VideoStageResult>::success(ds::VideoStageResult::RequestFrames);
    }
    return ds::Result<ds::VideoStageResult>::success(ds::VideoStageResult::Ready);
  }
};
}

TEST_CASE("VS staged batches release host references after adopting all input aliases", "[staged_video]") {
  auto api = staged_fake_api();
  int node_token = 0;
  auto* node = reinterpret_cast<VSNode*>(&node_token);
  const std::vector<VSNode*> nodes{node,node};
  const std::vector<ds::VideoInputInfo> inputs{{1,1,2},{1,1,2}};
  for (bool release : {false,true}) {
    StagedNativeFrame native;
    StagedNativeContext host;
    ds::FrameRef external;
    AliasedStages::State state{release};
    {
      ds::StagedVideoRequest<AliasedStages> request(0,state);
      while (!request.advance(inputs,state)) {
        REQUIRE(host.available.empty());
        host.available.emplace(std::make_pair(node,0),&native);
        ++native.references; // A separate frameCtx reference, not the DS2 owner.
        ds::vapoursynth::accept_staged_video_frames(request,nodes,
          reinterpret_cast<VSFrameContext*>(&host),nullptr,&api);
        CHECK(host.available.empty());
        CHECK(native.references == 2);
      }
      CHECK(host.gets == (release ? 3 : 2));
      CHECK(host.releases == (release ? 2 : 1));
      external = request.get(1,0).value().owner;
    }
    CHECK(native.references == 1);
    CHECK(*static_cast<const uint8_t*>(external.view().plane(0).data) == 37);
    external = {};
    CHECK(native.references == 0);
  }
}

TEST_CASE("VS staged partial delivery and missing early release unwind owning frames", "[staged_video]") {
  auto api = staged_fake_api();
  int node_token = 0;
  auto* node = reinterpret_cast<VSNode*>(&node_token);
  const std::vector<VSNode*> nodes{node,node};
  const std::vector<ds::VideoInputInfo> inputs{{1,1,2},{1,1,2}};
  AliasedStages::State state;
  StagedNativeFrame native;
  {
    StagedNativeContext host;
    host.available.emplace(std::make_pair(node,0),&native);
    ++native.references;
    {
      ds::StagedVideoRequest<AliasedStages> request(0,state);
      REQUIRE_FALSE(request.advance(inputs,state));
      api.releaseFrameEarly = nullptr;
      CHECK_THROWS_WITH(ds::vapoursynth::accept_staged_video_frames(request,nodes,
        reinterpret_cast<VSFrameContext*>(&host),nullptr,&api),
        "DualSynth: staged video requires VapourSynth releaseFrameEarly");
      CHECK(host.gets == 0);
      api.releaseFrameEarly = staged_fake_release;
      host.fail_get = 2;
      CHECK_THROWS_WITH(ds::vapoursynth::accept_staged_video_frames(request,nodes,
        reinterpret_cast<VSFrameContext*>(&host),nullptr,&api),
        "DualSynth: staged frame was not provided");
      CHECK(native.references == 2);
      CHECK(host.releases == 0); // Incomplete batches unwind with the host request.
    }
    CHECK(native.references == 1);
  }
  CHECK(native.references == 0);
}

namespace {
struct RequiredFrameServicesCore {
  static constexpr ds::HostRequirements host_requirements{true,0,0};
};
}
TEST_CASE("VapourSynth rejects incomplete opt-in frame services", "[frame_services]") {
  VSAPI api{};
  CHECK_THROWS_WITH(ds::vapoursynth::check_host_requirements<RequiredFrameServicesCore>(&api),
    "DualSynth: filter requires complete VapourSynth 4 frame services");
}

namespace {

struct SampleCore {
  static constexpr int input_count = 1;
};

struct SampleBridge : ds::SingleInputVideoBridgeDefaults<SampleCore> {
  static constexpr const char* vs_name = "Sample";
  static constexpr const char* avs_name = "DSSample";
  static constexpr const char* vs_format_error = "Sample requires GRAY8";
  static constexpr const char* avs_format_error = "DSSample requires Y8";
};

struct RecordingCreator {
  bool called = false;
  bool core_matches = false;
  std::string input_name;
  std::string missing_error;
  std::string format_error;

  template <class Filter>
  int operator()(
    const std::array<const char*, static_cast<std::size_t>(Filter::input_count)>& input_names,
    const char* missing,
    const char* format
  ) {
    called = true;
    core_matches = std::is_same_v<Filter, SampleCore>;
    input_name = input_names[0];
    missing_error = missing;
    format_error = format;
    return 7;
  }
};

struct FakeVSMap {
  using Value = std::variant<std::vector<std::int64_t>, std::vector<double>, std::vector<std::string>>;
  std::map<std::string, Value> values;
};

const FakeVSMap& fake_map(const VSMap* map) {
  return *reinterpret_cast<const FakeVSMap*>(map);
}

int VS_CC fake_map_num_elements(const VSMap* map, const char* key) noexcept {
  const auto& values = fake_map(map).values;
  const auto found = values.find(key);
  if (found == values.end()) {
    return -1;
  }
  return static_cast<int>(std::visit([](const auto& item) { return item.size(); }, found->second));
}

int64_t VS_CC fake_map_get_int(const VSMap* map, const char* key, int index, int* error) noexcept {
  const auto& values = fake_map(map).values;
  const auto found = values.find(key);
  if (found == values.end() || !std::holds_alternative<std::vector<std::int64_t>>(found->second)) {
    *error = peUnset;
    return 0;
  }
  *error = peSuccess;
  return std::get<std::vector<std::int64_t>>(found->second)[static_cast<std::size_t>(index)];
}

double VS_CC fake_map_get_float(const VSMap* map, const char* key, int index, int* error) noexcept {
  const auto& values = fake_map(map).values;
  const auto found = values.find(key);
  if (found == values.end() || !std::holds_alternative<std::vector<double>>(found->second)) {
    *error = peUnset;
    return 0.0;
  }
  *error = peSuccess;
  return std::get<std::vector<double>>(found->second)[static_cast<std::size_t>(index)];
}

const char* VS_CC fake_map_get_data(const VSMap* map, const char* key, int index, int* error) noexcept {
  const auto& values = fake_map(map).values;
  const auto found = values.find(key);
  if (found == values.end() || !std::holds_alternative<std::vector<std::string>>(found->second)) {
    *error = peUnset;
    return "";
  }
  *error = peSuccess;
  return std::get<std::vector<std::string>>(found->second)[static_cast<std::size_t>(index)].c_str();
}

VSAPI fake_vsapi() {
  VSAPI api{};
  api.mapNumElements = fake_map_num_elements;
  api.mapGetInt = fake_map_get_int;
  api.mapGetFloat = fake_map_get_float;
  api.mapGetData = fake_map_get_data;
  return api;
}

} // namespace

TEST_CASE("VapourSynth video bridge helper forwards bridge metadata to creator") {
  RecordingCreator creator;

  const int result = ds::vapoursynth::create_video_filter_bridge<SampleBridge>(creator);

  REQUIRE(result == 7);
  REQUIRE(creator.called);
  REQUIRE(creator.core_matches);
  REQUIRE(creator.input_name == "clip");
  REQUIRE(creator.missing_error == "DualSynth: missing required video clip");
  REQUIRE(creator.format_error == "Sample requires GRAY8");
}

TEST_CASE("VapourSynth parameter reader converts host values using descriptor metadata") {
  FakeVSMap map{
    {
      {"sigma", std::vector<double>{2.5}},
      {"planes", std::vector<std::int64_t>{0, 2}},
      {"enabled", std::vector<std::int64_t>{1}},
      {"labels", std::vector<std::string>{"y", "u"}},
      {"avs_only", std::vector<std::int64_t>{9}}
    }
  };
  const VSAPI api = fake_vsapi();
  const ds::FilterDescriptor descriptor{
    "Sample",
    std::vector<ds::ParamSpec>{
      ds::ParamSpec{"sigma", ds::ParamType::Float, ds::ParamValue{1.0}, false},
      ds::ParamSpec{"planes", ds::ParamType::Integer, ds::ParamValue{std::vector<std::int64_t>{}}, false, true},
      ds::ParamSpec{"enabled", ds::ParamType::Boolean, ds::ParamValue{false}, false},
      ds::ParamSpec{"labels", ds::ParamType::String, ds::ParamValue{std::vector<std::string>{}}, false, true},
      ds::ParamSpec{"avs_only", ds::ParamType::Integer, ds::ParamValue{0}, false, false, false, true}
    }
  };

  const auto result = ds::vapoursynth::read_params(
    reinterpret_cast<const VSMap*>(&map),
    descriptor,
    &api
  );

  REQUIRE(result.has_value());
  const auto& values = result.value();
  REQUIRE(values.get_double("sigma", 0.0).value() == 2.5);
  REQUIRE(values.get_int_array("planes", {}).value() == std::vector<std::int64_t>{0, 2});
  REQUIRE(values.get_bool("enabled", false).value());
  REQUIRE(values.get_string_array("labels", {}).value() == std::vector<std::string>{"y", "u"});
  REQUIRE(values.get_int("avs_only", 4).value() == 4);
}

TEST_CASE("VapourSynth video bridge converts host video formats to DualSynth formats") {
  const VSVideoFormat yuv420p10{
    cfYUV,
    stInteger,
    10,
    2,
    1,
    1,
    3
  };
  const VSVideoFormat rgbs{
    cfRGB,
    stFloat,
    32,
    4,
    0,
    0,
    3
  };

  const auto yuv = ds::vapoursynth::make_video_format(yuv420p10);
  const auto rgb = ds::vapoursynth::make_video_format(rgbs);

  REQUIRE(yuv.has_value());
  REQUIRE(yuv.value() == ds::VideoFormat{ds::ColorFamily::Yuv, ds::SampleFormat::UInt10, 3, 1, 1});
  REQUIRE(rgb.has_value());
  REQUIRE(rgb.value() == ds::VideoFormat{ds::ColorFamily::Rgb, ds::SampleFormat::Float32, 3, 0, 0});
}

TEST_CASE("VapourSynth rejects alpha and inconsistent layouts before querying the host") {
  VSVideoFormat output{};
  const ds::VideoFormat invalid[] = {
    {ds::ColorFamily::Rgb,ds::SampleFormat::UInt16,4,0,0},
    {ds::ColorFamily::Yuv,ds::SampleFormat::UInt16,4,1,1},
    {ds::ColorFamily::Gray,ds::SampleFormat::UInt8,2,0,0},
    {ds::ColorFamily::Yuv,ds::SampleFormat::UInt8,1,0,0},
    {ds::ColorFamily::Rgb,ds::SampleFormat::UInt8,3,1,0},
  };
  for (const auto& format : invalid)
    CHECK_FALSE(ds::vapoursynth::query_video_format(format,output,nullptr,nullptr));
}

TEST_CASE("VS output metadata rejects invalid geometry and timing", "[video_timing]") {
  ds::VideoOutputInfo output{16,8,4,{ds::ColorFamily::Yuv,ds::SampleFormat::UInt10,3,1,1},{24000,1001}};
  CHECK_NOTHROW(ds::vapoursynth::validate_output_video_info(output));
  for (auto fps : {ds::FrameRate{0,0}, ds::FrameRate{INT64_MAX,1}}) {
    auto test = output; test.fps = fps;
    CHECK_NOTHROW(ds::vapoursynth::validate_output_video_info(test));
  }
  for (auto fps : {ds::FrameRate{0,1}, ds::FrameRate{24,0}, ds::FrameRate{-1,1}, ds::FrameRate{1,-1}}) {
    auto test = output; test.fps = fps;
    CHECK_THROWS_WITH(ds::vapoursynth::validate_output_video_info(test), "DualSynth: invalid VapourSynth output frame rate");
  }
  for (int width : {0,-1,15,INT_MAX}) {
    auto test = output; test.width = width;
    CHECK_THROWS_AS(ds::vapoursynth::validate_output_video_info(test), std::invalid_argument);
  }
  for (int height : {0,-1,7}) {
    auto test = output; test.height = height;
    CHECK_THROWS_AS(ds::vapoursynth::validate_output_video_info(test), std::invalid_argument);
  }
  for (int count : {0,-1}) {
    auto test = output; test.num_frames = count;
    CHECK_THROWS_WITH(ds::vapoursynth::validate_output_video_info(test), "DualSynth: VapourSynth output frame count must be positive");
  }
}

namespace {
int metadata_vs_type;
int VS_CC metadata_vs_count(const VSMap*, const char*) noexcept { return 2; }
int VS_CC metadata_vs_get_type(const VSMap*, const char*) noexcept { return metadata_vs_type; }
const VSMap* VS_CC metadata_vs_props(const VSFrame*) noexcept { return nullptr; }
}
TEST_CASE("VS metadata retains categories of unreadable host objects", "[frame_services]") {
  VSAPI api{};
  api.getFramePropertiesRO = metadata_vs_props;
  api.mapNumElements = metadata_vs_count;
  api.mapGetType = metadata_vs_get_type;
  const VSFrame* frame = nullptr;
  ds::vapoursynth::FrameTraits traits{&api,nullptr};
  ds::detail::NativeProperties<ds::vapoursynth::FrameTraits> props(traits,frame,false);
  for (auto item : {std::pair{ptVideoNode,ds::PropertyType::VideoNode},
                   {ptAudioNode,ds::PropertyType::AudioNode},
                   {ptAudioFrame,ds::PropertyType::AudioFrame},
                   {ptFunction,ds::PropertyType::Function}}) {
    metadata_vs_type = item.first;
    const auto info = props.inspect("opaque");
    REQUIRE(info.has_value());
    CHECK(info->type == item.second);
    CHECK(info->count == 2);
    CHECK_THROWS_AS(props.find("opaque"),std::invalid_argument);
  }
}
