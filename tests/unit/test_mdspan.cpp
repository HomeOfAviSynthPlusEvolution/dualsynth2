#include <catch2/catch_test_macros.hpp>
#include <dualsynth/mdspan.hpp>
#include <array>
#include <type_traits>

TEST_CASE("PlaneView2D exposes an mdspan stride view") {
  using Expected = std::mdspan<
    unsigned short,
    std::dextents<std::size_t, 2>,
    std::layout_stride
  >;

  STATIC_REQUIRE(std::is_same_v<ds::PlaneView2D<unsigned short>, Expected>);
}

TEST_CASE("PlaneView2D indexes stride-backed mutable planes") {
  std::array<unsigned short, 8> storage{
    1, 2, 3, 99,
    4, 5, 6, 88
  };

  auto view = ds::make_plane_view(storage.data(), 3, 2, 4 * static_cast<int>(sizeof(unsigned short)));

  REQUIRE(view.extent(0) == 2);
  REQUIRE(view.extent(1) == 3);
  REQUIRE(view[0, 0] == 1);
  REQUIRE(view[0, 2] == 3);
  REQUIRE(view[1, 0] == 4);

  view[1, 2] = 42;

  REQUIRE(storage[6] == 42);
  REQUIRE(storage[7] == 88);
}

TEST_CASE("PlaneView2D supports const sample views") {
  const std::array<unsigned char, 6> storage{
    10, 11, 99,
    20, 21, 88
  };

  auto view = ds::make_plane_view(storage.data(), 2, 2, 3);

  REQUIRE(view.extent(0) == 2);
  REQUIRE(view.extent(1) == 2);
  REQUIRE(view[0, 1] == 11);
  REQUIRE(view[1, 0] == 20);
}
