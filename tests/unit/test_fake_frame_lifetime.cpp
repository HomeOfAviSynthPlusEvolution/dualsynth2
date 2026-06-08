#include <catch2/catch_test_macros.hpp>
#include <dualsynth/frame.hpp>
#include <array>
#include <cstdint>

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

TEST_CASE("Video frame view exposes typed mdspan planes") {
  std::array<std::uint16_t, 8> storage{
    1, 2, 3, 99,
    4, 5, 6, 88
  };

  ds::MutableVideoFrameView frame{
    ds::VideoFormat{ds::ColorFamily::Gray, ds::SampleFormat::UInt16, 1, 0, 0},
    1,
    std::array<ds::MutablePlaneView, 4>{
      ds::MutablePlaneView{storage.data(), 4 * static_cast<std::ptrdiff_t>(sizeof(std::uint16_t)), 3, 2},
      ds::MutablePlaneView{},
      ds::MutablePlaneView{},
      ds::MutablePlaneView{}
    }
  };

  auto plane = ds::as_plane_view<std::uint16_t>(frame.plane(0));

  REQUIRE(plane.extent(0) == 2);
  REQUIRE(plane.extent(1) == 3);
  REQUIRE(plane[1, 2] == 6);

  plane[1, 2] = 42;

  REQUIRE(storage[6] == 42);
  REQUIRE(storage[7] == 88);
}
