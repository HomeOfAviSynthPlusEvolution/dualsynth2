#include <catch2/catch_test_macros.hpp>
#include <dualsynth/video_bridge.hpp>
#include <dualsynth/video_filter.hpp>
#include <string>
#include <type_traits>

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
