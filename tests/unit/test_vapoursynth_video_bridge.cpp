#include <catch2/catch_test_macros.hpp>
#include <dualsynth/vapoursynth/video_bridge.hpp>
#include <dualsynth/video_bridge.hpp>
#include <array>
#include <string>
#include <type_traits>

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
