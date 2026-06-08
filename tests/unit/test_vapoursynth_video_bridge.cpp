#include <catch2/catch_test_macros.hpp>
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
