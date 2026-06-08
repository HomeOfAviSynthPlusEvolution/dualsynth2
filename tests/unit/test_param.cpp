#include <catch2/catch_test_macros.hpp>
#include <dualsynth/param.hpp>
#include <cstdint>
#include <string>
#include <vector>

TEST_CASE("Parameter schema stores standard value defaults") {
  const ds::ParamSpec strength{
    "strength",
    ds::ParamType::Float,
    ds::ParamValue{1.0},
    true
  };

  REQUIRE(strength.name == "strength");
  REQUIRE(strength.type == ds::ParamType::Float);
  REQUIRE(strength.required);
  REQUIRE(std::get<double>(strength.default_value.value) == 1.0);
}

TEST_CASE("Parameter schema records array and host availability metadata") {
  const ds::ParamSpec planes{
    "planes",
    ds::ParamType::Integer,
    ds::ParamValue{std::vector<std::int64_t>{0, 1, 2}},
    false,
    true,
    true,
    false
  };

  REQUIRE_FALSE(planes.required);
  REQUIRE(planes.is_array);
  REQUIRE(planes.vs_enabled);
  REQUIRE_FALSE(planes.avs_enabled);
  REQUIRE(std::get<std::vector<std::int64_t>>(planes.default_value.value) == std::vector<std::int64_t>{0, 1, 2});
}

TEST_CASE("Parameter validation rejects empty names") {
  const ds::ParamSpec spec{
    "",
    ds::ParamType::Integer,
    ds::ParamValue{3},
    true
  };

  const auto result = ds::validate_param_spec(spec);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error().code == ds::ErrorCode::InvalidArgument);
}

TEST_CASE("Parameter validation rejects defaults that do not match array metadata") {
  const ds::ParamSpec spec{
    "planes",
    ds::ParamType::Integer,
    ds::ParamValue{0},
    false,
    true
  };

  const auto result = ds::validate_param_spec(spec);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error().code == ds::ErrorCode::InvalidArgument);
}

TEST_CASE("Parameter values read integers with defaults") {
  ds::ParamValues values{
    std::vector<ds::ParamEntry>{
      ds::ParamEntry{"width", ds::ParamValue{1280}}
    }
  };

  REQUIRE(values.get_int("width", 640).value() == 1280);
  REQUIRE(values.get_int("height", 480).value() == 480);
}

TEST_CASE("Parameter values reject wrong integer types") {
  ds::ParamValues values{
    std::vector<ds::ParamEntry>{
      ds::ParamEntry{"width", ds::ParamValue{1.0}}
    }
  };

  const auto result = values.get_int("width", 640);

  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error().code == ds::ErrorCode::InvalidArgument);
}

TEST_CASE("Parameter values read scalar types with defaults") {
  ds::ParamValues values{
    std::vector<ds::ParamEntry>{
      ds::ParamEntry{"count", ds::ParamValue{std::int64_t{7}}},
      ds::ParamEntry{"sigma", ds::ParamValue{2.5}},
      ds::ParamEntry{"enabled", ds::ParamValue{true}},
      ds::ParamEntry{"mode", ds::ParamValue{std::string{"fast"}}}
    }
  };

  REQUIRE(values.get_int64("count", 0).value() == 7);
  REQUIRE(values.get_double("sigma", 0.0).value() == 2.5);
  REQUIRE(values.get_bool("enabled", false).value());
  REQUIRE(values.get_string("mode", "slow").value() == "fast");
  REQUIRE(values.get_int64("missing_count", 9).value() == 9);
  REQUIRE(values.get_double("missing_sigma", 1.25).value() == 1.25);
  REQUIRE(values.get_bool("missing_enabled", true).value());
  REQUIRE(values.get_string("missing_mode", "default").value() == "default");
}

TEST_CASE("Parameter values read array types with defaults") {
  ds::ParamValues values{
    std::vector<ds::ParamEntry>{
      ds::ParamEntry{"planes", ds::ParamValue{std::vector<std::int64_t>{0, 2}}},
      ds::ParamEntry{"weights", ds::ParamValue{std::vector<double>{0.5, 1.5}}},
      ds::ParamEntry{"enabled", ds::ParamValue{std::vector<bool>{true, false}}},
      ds::ParamEntry{"labels", ds::ParamValue{std::vector<std::string>{"y", "u"}}}
    }
  };

  REQUIRE(values.get_int_array("planes", {}).value() == std::vector<std::int64_t>{0, 2});
  REQUIRE(values.get_double_array("weights", {}).value() == std::vector<double>{0.5, 1.5});
  REQUIRE(values.get_bool_array("enabled", {}).value() == std::vector<bool>{true, false});
  REQUIRE(values.get_string_array("labels", {}).value() == std::vector<std::string>{"y", "u"});
  REQUIRE(values.get_int_array("missing_planes", {1, 2}).value() == std::vector<std::int64_t>{1, 2});
}

TEST_CASE("Parameter values parse string fallbacks for numeric arrays") {
  ds::ParamValues values{
    std::vector<ds::ParamEntry>{
      ds::ParamEntry{"planes", ds::ParamValue{"0, 2 3"}},
      ds::ParamEntry{"weights", ds::ParamValue{"0.5, 1.5 2.25"}},
      ds::ParamEntry{"flags", ds::ParamValue{"true, false 1 0"}}
    }
  };

  REQUIRE(values.get_int_array("planes", {}).value() == std::vector<std::int64_t>{0, 2, 3});
  REQUIRE(values.get_double_array("weights", {}).value() == std::vector<double>{0.5, 1.5, 2.25});
  REQUIRE(values.get_bool_array("flags", {}).value() == std::vector<bool>{true, false, true, false});
}
