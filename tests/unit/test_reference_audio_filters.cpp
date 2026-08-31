#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "audio_filters.hpp"

TEST_CASE("Reference audio identity copies samples") {
  const std::array<float, 4> src{0.0F, 0.25F, -0.5F, 1.0F};
  std::array<float, 4> dst{};

  ds::reference::copy_samples(ds::Span<const float>(src), ds::Span<float>(dst));

  REQUIRE(dst == src);
}

TEST_CASE("Reference audio gain scales float samples") {
  const std::array<float, 3> src{0.25F, -0.5F, 1.0F};
  std::array<float, 3> dst{};

  ds::reference::gain_samples(ds::Span<const float>(src), ds::Span<float>(dst), 2.0);

  REQUIRE(dst[0] == Catch::Approx(0.5F));
  REQUIRE(dst[1] == Catch::Approx(-1.0F));
  REQUIRE(dst[2] == Catch::Approx(2.0F));
}

TEST_CASE("Reference audio gain clamps integer samples") {
  const std::array<short, 4> src{1000, -1000, 20000, -20000};
  std::array<short, 4> dst{};

  ds::reference::gain_samples(ds::Span<const short>(src), ds::Span<short>(dst), 2.0);

  REQUIRE(dst == std::array<short, 4>{2000, -2000, 32767, -32768});
}
