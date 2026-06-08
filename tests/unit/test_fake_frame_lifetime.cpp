#include <catch2/catch_test_macros.hpp>
#include <dualsynth/frame.hpp>

TEST_CASE("FrameHandle releases through custom deleter") {
  int release_count = 0;

  {
    ds::FrameHandle handle(
      &release_count,
      [](void* user) {
        auto* count = static_cast<int*>(user);
        ++(*count);
      }
    );

    REQUIRE(handle.valid());
    REQUIRE(release_count == 0);
  }

  REQUIRE(release_count == 1);
}

TEST_CASE("FrameHandle move transfers ownership") {
  int release_count = 0;

  {
    ds::FrameHandle first(
      &release_count,
      [](void* user) {
        auto* count = static_cast<int*>(user);
        ++(*count);
      }
    );

    ds::FrameHandle second(std::move(first));

    REQUIRE_FALSE(first.valid());
    REQUIRE(second.valid());
  }

  REQUIRE(release_count == 1);
}
