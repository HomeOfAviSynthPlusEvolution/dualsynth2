#include <catch2/catch_test_macros.hpp>
#include <dualsynth/param.hpp>

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
