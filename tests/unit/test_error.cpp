#include <catch2/catch_test_macros.hpp>
#include <dualsynth/dualsynth.hpp>

TEST_CASE("DualSynth exposes an initial version") {
  STATIC_REQUIRE(ds::version_major == 0);
  STATIC_REQUIRE(ds::version_minor == 1);
  STATIC_REQUIRE(ds::version_patch == 0);
}
