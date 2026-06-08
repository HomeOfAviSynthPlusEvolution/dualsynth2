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
