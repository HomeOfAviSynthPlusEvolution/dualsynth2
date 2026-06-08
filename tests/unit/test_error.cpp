#include <catch2/catch_test_macros.hpp>
#include <dualsynth/error.hpp>

TEST_CASE("Error stores code and message") {
  const ds::Error error{
    ds::ErrorCode::UnsupportedFormat,
    "convert to planar uint8, uint16, or float32"
  };

  REQUIRE(error.code == ds::ErrorCode::UnsupportedFormat);
  REQUIRE(error.message == "convert to planar uint8, uint16, or float32");
}

TEST_CASE("Result can hold a value") {
  const auto value = ds::Result<int>::success(42);

  REQUIRE(value.has_value());
  REQUIRE(value.value() == 42);
}

TEST_CASE("Result can hold an error") {
  const auto value = ds::Result<int>::failure({
    ds::ErrorCode::InvalidArgument,
    "radius must be non-negative"
  });

  REQUIRE_FALSE(value.has_value());
  REQUIRE(value.error().code == ds::ErrorCode::InvalidArgument);
  REQUIRE(value.error().message == "radius must be non-negative");
}
