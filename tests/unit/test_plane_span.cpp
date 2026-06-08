#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <dualsynth/plane_span.hpp>

TEST_CASE("PlaneSpan indexes rows with byte stride") {
  std::array<unsigned short, 12> storage{};
  storage[0] = 1;
  storage[1] = 2;
  storage[4] = 5;
  storage[5] = 6;

  ds::PlaneSpan<unsigned short> plane(
    storage.data(),
    2,
    2,
    static_cast<std::ptrdiff_t>(4 * sizeof(unsigned short))
  );

  REQUIRE(plane.height() == 2);
  REQUIRE(plane.width() == 2);
  REQUIRE(plane(0, 0) == 1);
  REQUIRE(plane(0, 1) == 2);
  REQUIRE(plane(1, 0) == 5);
  REQUIRE(plane(1, 1) == 6);
}

TEST_CASE("PlaneSpan row returns a standard span") {
  std::array<unsigned char, 8> storage{0, 1, 2, 3, 4, 5, 6, 7};

  ds::PlaneSpan<unsigned char> plane(storage.data(), 4, 2, 4);
  auto row = plane.row(1);

  REQUIRE(row.size() == 4);
  REQUIRE(row[0] == 4);
  REQUIRE(row[3] == 7);
}
