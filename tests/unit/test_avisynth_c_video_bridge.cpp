#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/catch_approx.hpp>

#include <avisynth_c.h>
#include <dualsynth/avisynth/c/video_bridge.hpp>
#include <list>
#include "stage_probe.hpp"

namespace {
struct ScopedApi {
  ds::avisynth::c::CApi previous = ds::avisynth::c::CApi::instance();
  ScopedApi() { auto& api = ds::avisynth::c::CApi::instance(); api = {}; api.loaded = true; }
  ~ScopedApi() { ds::avisynth::c::CApi::instance() = previous; }
};
std::list<std::string> saved_errors;
int released_frames = 0;
int fake_frame_storage;
char* AVSC_CC test_save_string(AVS_ScriptEnvironment*, const char* value, int) {
  saved_errors.emplace_back(value);
  return saved_errors.back().data();
}
AVS_VideoFrame* AVSC_CC test_new_frame(AVS_ScriptEnvironment*,const AVS_VideoInfo*,const AVS_VideoFrame*) {
  return reinterpret_cast<AVS_VideoFrame*>(&fake_frame_storage);
}
AVS_VideoFrame* AVSC_CC test_new_frame_no_props(AVS_ScriptEnvironment* env,const AVS_VideoInfo* vi,int) { return test_new_frame(env,vi,nullptr); }
void AVSC_CC test_release_frame(AVS_VideoFrame*) { ++released_frames; }
struct ThrowingCore {
  static constexpr int input_count = 1;
  static constexpr ds::OutputOrigin output_origin = ds::OutputOrigin::fresh_without_props();
  static ds::Result<ds::VideoProcessResult> process(ds::VideoProcessContext&) {
    throw std::runtime_error(std::string("temporary error: 100% complete"));
  }
};
struct ThrowingBridge { using Core = ThrowingCore; };
struct RequiredServices { static constexpr ds::HostRequirements host_requirements{true,11,0}; };
int AVSC_CC test_check_version(AVS_ScriptEnvironment*,int) { return 0; }
}

TEST_CASE("AviSynth C process exceptions release output and preserve error text", "[frame_services]") {
  ScopedApi restore;
  auto& api = ds::avisynth::c::CApi::instance();
  api.new_video_frame_p = test_new_frame;
  api.new_video_frame_a = test_new_frame_no_props;
  api.release_video_frame = test_release_frame;
  api.save_string = test_save_string;
  released_frames = 0;
  ds::avisynth::c::CVideoFilterStateHolder<ThrowingBridge> holder{};
  holder.output_format.plane_count = 0; // No pixel access needed by this failing filter.
  AVS_FilterInfo fi{};
  fi.user_data = &holder;
  CHECK(ds::avisynth::c::c_filter_get_frame<ThrowingBridge>(&fi,0) == nullptr);
  CHECK(released_frames == 1);
  REQUIRE(fi.error != nullptr);
  CHECK(std::string(fi.error) == "temporary error: 100% complete");
  saved_errors.clear();
}

TEST_CASE("AviSynth C rejects insufficient versions and incomplete frame APIs", "[frame_services]") {
  ScopedApi restore;
  CHECK_THROWS_WITH(ds::avisynth::c::check_host_requirements<RequiredServices>(nullptr),
    "DualSynth: filter requires AviSynth interface 11");
  ds::avisynth::c::CApi::instance().check_version = test_check_version;
  CHECK_THROWS_WITH(ds::avisynth::c::check_host_requirements<RequiredServices>(nullptr),
    "DualSynth: required AviSynth frame service entry point is missing");
}

TEST_CASE("AviSynth C format mapping matches standard formats", "[avisynth_c]") {
  AVS_VideoInfo vi{};
  vi.width = 1920;
  vi.height = 1080;
  vi.fps_numerator = 24;
  vi.fps_denominator = 1;
  vi.num_frames = 100;
  vi.pixel_type = AVS_CS_Y8;

  const auto format = ds::avisynth::c::make_video_format(vi);
  REQUIRE(format.has_value());
  CHECK(format.value().color_family == ds::ColorFamily::Gray);
  CHECK(format.value().sample_format == ds::SampleFormat::UInt8);
  CHECK(format.value().plane_count == 1);

  const auto back_vi = ds::avisynth::c::make_video_info(
    ds::VideoOutputInfo{1920, 1080, 100, format.value(), ds::FrameRate{24, 1}}
  );
  CHECK(back_vi.pixel_type == AVS_CS_Y8);
  CHECK(back_vi.width == 1920);
  CHECK(back_vi.height == 1080);
}

TEST_CASE("AvisynthCValueParamSource reads 64-bit aware integers, floats, booleans, and arrays", "[avisynth_c]") {
  const ds::FilterDescriptor descriptor{
    "TestFilter",
    std::vector<ds::ParamSpec>{
      ds::ParamSpec{"clip", ds::ParamType::Clip, ds::ParamValue{}, true},
      ds::ParamSpec{"req_int", ds::ParamType::Integer, ds::ParamValue{}, true},
      ds::ParamSpec{"opt_float", ds::ParamType::Float, ds::ParamValue{2.5}, false},
      ds::ParamSpec{"opt_bool", ds::ParamType::Boolean, ds::ParamValue{true}, false},
      ds::ParamSpec{"str_val", ds::ParamType::String, ds::ParamValue{std::string("default")}, false},
      ds::ParamSpec{"arr_int", ds::ParamType::Integer, ds::ParamValue{std::vector<std::int64_t>{}}, false, true}
    }
  };

  AVS_Value elements[6];
  // 0: clip (skipped in scalar parsing)
  elements[0].type = 'c';
  elements[0].d.clip = nullptr;
  // 1: req_int
  elements[1].type = 'i';
  elements[1].d.integer = 42;
  // 2: opt_float
  elements[2].type = 'f';
  elements[2].d.floating_pt = 3.14f;
  // 3: opt_bool
  elements[3].type = 'b';
  elements[3].d.boolean = 1;
  // 4: str_val
  elements[4].type = 's';
  elements[4].d.string = "hello";
  // 5: arr_int as string fallback "1, 2, 3"
  elements[5].type = 's';
  elements[5].d.string = "1, 2, 3";

  AVS_Value args;
  args.type = 'a';
  args.array_size = 6;
  args.d.array = elements;

  const auto result = ds::avisynth::c::read_params(args, descriptor);
  REQUIRE(result.has_value());
  const auto& values = result.value();
  CHECK(values.get_int("req_int", 0).value() == 42);
  CHECK(values.get_double("opt_float", 0.0).value() == Catch::Approx(3.14));
  CHECK(values.get_bool("opt_bool", false).value() == true);
  CHECK(values.get_string("str_val", "").value() == "hello");
  CHECK(values.get_int_array("arr_int", {}).value() == std::vector<std::int64_t>{1, 2, 3});
}

TEST_CASE("AviSynth C bridge MT mode and plane id mappings", "[avisynth_c]") {
  CHECK(ds::avisynth::c::host_mt_mode(ds::avisynth::c::MtMode::NiceFilter) == AVS_MT_NICE_FILTER);
  CHECK(ds::avisynth::c::host_mt_mode(ds::avisynth::c::MtMode::MultiInstance) == AVS_MT_MULTI_INSTANCE);
  CHECK(ds::avisynth::c::host_mt_mode(ds::avisynth::c::MtMode::Serialized) == AVS_MT_SERIALIZED);

  const ds::VideoFormat yuv{ds::ColorFamily::Yuv, ds::SampleFormat::UInt8, 3, 1, 1};
  CHECK(ds::avisynth::c::plane_id(yuv, 0) == AVS_PLANAR_Y);
  CHECK(ds::avisynth::c::plane_id(yuv, 1) == AVS_PLANAR_U);
  CHECK(ds::avisynth::c::plane_id(yuv, 2) == AVS_PLANAR_V);

  const ds::VideoFormat rgb{ds::ColorFamily::Rgb, ds::SampleFormat::UInt8, 3, 0, 0};
  CHECK(ds::avisynth::c::plane_id(rgb, 0) == AVS_PLANAR_R);
  CHECK(ds::avisynth::c::plane_id(rgb, 1) == AVS_PLANAR_G);
  CHECK(ds::avisynth::c::plane_id(rgb, 2) == AVS_PLANAR_B);
}

TEST_CASE("AviSynth C bridge output origin matching", "[avisynth_c]") {
  const ds::VideoFormat format{ds::ColorFamily::Gray, ds::SampleFormat::UInt8, 1, 0, 0};
  const ds::VideoOutputInfo output{640, 480, 100, format, ds::FrameRate{24, 1}};
  const std::vector<ds::VideoInputInfo> inputs{
    ds::VideoInputInfo{640, 480, 100, format, ds::FrameRate{24, 1}},
    ds::VideoInputInfo{320, 240, 100, format, ds::FrameRate{24, 1}}
  };

  CHECK(ds::avisynth::c::output_origin_matches(
    ds::OutputOrigin::take_from_input(0), output, ds::Span<const ds::VideoInputInfo>(inputs.data(), inputs.size())
  ));

  CHECK_FALSE(ds::avisynth::c::output_origin_matches(
    ds::OutputOrigin::take_from_input(1), output, ds::Span<const ds::VideoInputInfo>(inputs.data(), inputs.size())
  ));

  CHECK(ds::avisynth::c::output_origin_matches(
    ds::OutputOrigin::fresh(), output, ds::Span<const ds::VideoInputInfo>(inputs.data(), inputs.size())
  ));
}

namespace {
struct CustomFilterCore {
  static constexpr const char* name = "CustomFilter";
  static constexpr int input_count = 1;
};

struct CustomFilterBridge : ds::SingleInputVideoBridgeDefaults<CustomFilterCore> {
  static constexpr const char* vs_name = "CustomFilter";
  static constexpr const char* avs_name = "DSCustomFilter";
  static constexpr ds::avisynth::MtMode avs_mt_mode = ds::avisynth::MtMode::MultiInstance;

  static ds::FilterDescriptor descriptor() {
    return ds::FilterDescriptor{
      "DSCustomFilter",
      std::vector<ds::ParamSpec>{
        ds::ParamSpec{"clip", ds::ParamType::Clip, ds::ParamValue{}, true},
        ds::ParamSpec{"range", ds::ParamType::Integer, ds::ParamValue{15}, false},
        ds::ParamSpec{"y", ds::ParamType::Integer, ds::ParamValue{64}, false}
      }
    };
  }
};
} // namespace

TEST_CASE("AviSynth C bridge generates dynamic signature from descriptor and matches ds::avisynth::MtMode", "[avisynth_c]") {
  CHECK(ds::avisynth::c::bridge_mt_mode<CustomFilterBridge>() == ds::avisynth::MtMode::MultiInstance);
  CHECK(std::string(ds::avisynth::c::bridge_avs_signature<CustomFilterBridge>()) == "c[range]i[y]i");
}

TEST_CASE("ds::avisynth::c rejects native integer depths unavailable in AviSynth") {
  for (int depth : {9,11,13,15}) {
    const auto sample = ds::sample_format_from_depth(false,depth).value();
    for (auto family : {ds::ColorFamily::Gray,ds::ColorFamily::Yuv,ds::ColorFamily::Rgb}) {
      ds::VideoFormat format{family,sample,family == ds::ColorFamily::Gray ? 1 : 3,0,0};
      CHECK(ds::avisynth::c::pixel_type(format) == AVS_CS_UNKNOWN);
      ds::avisynth::c::FrameTraits traits{};
      CHECK_THROWS_AS(traits.allocate(format,16,8,{}),std::invalid_argument);
    }
  }
}

TEST_CASE("ds::avisynth::c rejects inconsistent layouts instead of normalizing them") {
  CHECK(ds::avisynth::c::pixel_type({ds::ColorFamily::Gray,ds::SampleFormat::UInt8,2,0,0}) == AVS_CS_UNKNOWN);
  CHECK(ds::avisynth::c::pixel_type({ds::ColorFamily::Rgb,ds::SampleFormat::UInt8,3,1,0}) == AVS_CS_UNKNOWN);
}

TEST_CASE("ds::avisynth::c output timing rejects narrowing and invalid denominators", "[video_timing]") {
  ds::VideoOutputInfo output{};
  constexpr std::int64_t maximum = 0xffffffffLL;
  for (const auto fps : {ds::FrameRate{24000, 1001}, ds::FrameRate{0, 1},
                        ds::FrameRate{maximum, maximum - 1}}) {
    output.fps = fps;
    const auto vi = ds::avisynth::c::make_video_info(output);
    CHECK(vi.fps_numerator == fps.numerator);
    CHECK(vi.fps_denominator == fps.denominator);
  }
  for (const auto fps : {ds::FrameRate{-1, 1}, ds::FrameRate{24, -1},
                        ds::FrameRate{24, 0}, ds::FrameRate{maximum + 1, 1},
                        ds::FrameRate{1, maximum + 1}}) {
    output.fps = fps;
    CHECK_THROWS_WITH(ds::avisynth::c::make_video_info(output),
      "DualSynth: output frame rate cannot be represented by AviSynth");
  }
}

TEST_CASE("AviSynth checked parameters distinguish omission, emptiness and invalid types", "[avs_params]") {
  using namespace ds;
  FilterDescriptor descriptor{"Params", {
    {"ints", ParamType::Integer, {}, false, true, true, true, AvisynthArrayBinding::Native},
    {"floats", ParamType::Float, {}, false, true, true, true, AvisynthArrayBinding::Native},
    {"legacy", ParamType::Integer, {}, false, true},
    {"flag", ParamType::Boolean},
    {"number", ParamType::Integer}
  }};
  AVS_Value good[] = {avs_new_value_int(7), avs_new_value_int(-2)};
  AVS_Value args[] = {avs_new_value_array(good, 2), avs_new_value_array(nullptr, 0),
    avs_void, avs_new_value_bool(0), avs_new_value_int(0), avs_new_value_array(good, 2)};
  auto read = [&] { return ds::avisynth::c::read_params(avs_new_value_array(args, 6), descriptor); };
  auto result = read();
  REQUIRE(result.has_value());
  CHECK(result.value().get_int_array("ints", {}).value() == std::vector<std::int64_t>{7,-2});
  CHECK(result.value().get_int_array("legacy", {}).value() == std::vector<std::int64_t>{7,-2});
  CHECK(result.value().get_double_array("floats", {123}).value().empty());
  CHECK_FALSE(result.value().get_bool("flag", true).value());
  CHECK(result.value().get_int64("number", 123).value() == 0);
  args[0] = avs_void;
  result = read();
  REQUIRE(result.has_value());
  CHECK(result.value().get_int_array("ints", {123}).value() == std::vector<std::int64_t>{123});
  descriptor.params[0].required = true;
  CHECK_FALSE(read().has_value());
  args[0] = avs_new_value_array(nullptr, 0);
  CHECK(read().has_value());
  args[0] = avs_new_value_int(3);
  result = read();
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().message == "AviSynth parameter 'ints': expected array");
  args[0] = avs_new_value_array(good, 2);
  good[1] = avs_new_value_string("wrong");
  result = read();
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().message == "AviSynth parameter 'ints': element[1]: expected integer");
  good[1] = avs_new_value_int(4);
  args[3] = avs_new_value_int(0);
  result = read();
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().message == "AviSynth parameter 'flag': expected boolean");
  args[3] = avs_new_value_bool(0);
  args[5] = avs_new_value_int(1);
  result = read();
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().message == "AviSynth parameter 'legacy': expected array");
}

TEST_CASE("AviSynth C validates all scalar and array element types before reading unions", "[avs_params]") {
  ds::avisynth::c::AvisynthCValueParamSource source(avs_new_value_bool(0));
  CHECK_THROWS(source.as_int(0));
  CHECK_THROWS(source.as_float(0));
  CHECK_THROWS(source.as_string(0));
  AVS_Value element = avs_void;
  element.type = 'c';
  element.d.clip = nullptr;
  AVS_Value array = avs_new_value_array(&element, 1);
  AVS_Value args = avs_new_value_array(&array, 1);
  ds::avisynth::c::AvisynthCValueParamSource arrays(args);
  CHECK_THROWS_WITH(arrays.as_int_array(0), "element[0]: expected integer");
  CHECK_THROWS_WITH(arrays.as_float_array(0), "element[0]: expected number");
  CHECK_THROWS_WITH(arrays.as_bool_array(0), "element[0]: expected boolean");
  CHECK_THROWS_WITH(arrays.as_string_array(0), "element[0]: expected string");
}

namespace {
int inspect_count = 0;
char inspect_type = 'i';
const AVS_Map* AVSC_CC metadata_props(AVS_ScriptEnvironment*, const AVS_VideoFrame*) { return nullptr; }
int AVSC_CC metadata_count(AVS_ScriptEnvironment*, const AVS_Map*, const char*) { return inspect_count; }
char AVSC_CC metadata_type(AVS_ScriptEnvironment*, const AVS_Map*, const char*) { return inspect_type; }
}
TEST_CASE("Property inspection reads metadata without accessing any values", "[frame_services]") {
  ScopedApi restore;
  auto& api = ds::avisynth::c::CApi::instance();
  api.get_frame_props_ro = metadata_props;
  api.prop_num_elements = metadata_count;
  api.prop_get_type = metadata_type;
  // All element getters stay null: inspection must never call them.
  AVS_VideoFrame* frame = nullptr;
  ds::avisynth::c::FrameTraits traits{nullptr,&api};
  ds::detail::NativeProperties<ds::avisynth::c::FrameTraits> props(traits,frame,false);
  inspect_count = -1;
  CHECK_FALSE(props.inspect("missing"));
  for (auto count : {0,1,17}) {
    inspect_count = count;
    for (auto item : {std::pair{'i',ds::PropertyType::Integer}, {'f',ds::PropertyType::Float},
                     {'s',ds::PropertyType::Data}, {'v',ds::PropertyType::VideoFrame},
                     {'c',ds::PropertyType::VideoNode}, {'?',ds::PropertyType::Unknown}}) {
      inspect_type = item.first;
      auto info = props.inspect("key");
      REQUIRE(info.has_value());
      CHECK(info->type == item.second);
      CHECK(info->count == static_cast<std::size_t>(count));
    }
  }
  CHECK_THROWS_AS(props.inspect(""),std::invalid_argument);
  CHECK_THROWS_AS(props.inspect(std::string("a\0b",3)),std::invalid_argument);
}

namespace {
void AVSC_CC ownership_copy(AVS_Value*, AVS_Value) { FAIL("must reject before copying values"); }
void AVSC_CC ownership_release(AVS_Value) { FAIL("must not create values without ownership APIs"); }
}
TEST_CASE("AviSynth C bundle creation rejects missing version and ownership APIs", "[avs_bundle]") {
  ScopedApi restore;
  auto& api = ds::avisynth::c::CApi::instance();
  api.save_string = test_save_string;
  auto check = [&] {
    const auto value = ds::avisynth::c::create_video_filter_bundle<ds::acceptance::StageProbeBridge>({},nullptr);
    REQUIRE(avs_is_error(value));
    CHECK(std::string(avs_as_error(value)) ==
      "DualSynth: clip arrays require AviSynth interface 11 and value ownership APIs");
    CHECK(ds::acceptance::live_stage_instances.load() == 0);
  };
  check();
  api.check_version = test_check_version;
  check();
  api.release_value = ownership_release;
  check();
  api.release_value = nullptr;
  api.copy_value = ownership_copy;
  check();
  saved_errors.clear();
}
