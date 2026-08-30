#include <catch2/catch_test_macros.hpp>
#include <dualsynth/span2d.hpp>

#include <cstdint>
#include <type_traits>
#include <vector>

TEST_CASE("span2d Row provides lightweight 1D continuous row slicing") {
  std::vector<std::uint8_t> buffer = {10, 20, 30, 40, 50};
  span2d::Row<std::uint8_t> row(buffer.data(), buffer.size());

  STATIC_REQUIRE(sizeof(span2d::Row<std::uint8_t>) == 16);
  STATIC_REQUIRE(sizeof(span2d::Row<const std::uint8_t>) == 16);

  REQUIRE(row.size() == 5);
  REQUIRE(row.size_bytes() == 5);
  REQUIRE_FALSE(row.empty());
  REQUIRE(row.data() == buffer.data());

  REQUIRE(row[0] == 10);
  REQUIRE(row[4] == 50);

  row[2] = 99;
  REQUIRE(buffer[2] == 99);

  // Conversion from Row<T> to Row<const T>
  span2d::Row<const std::uint8_t> const_row = row;
  REQUIRE(const_row.size() == 5);
  REQUIRE(const_row[2] == 99);

  // Subspan
  auto sub = row.subspan(1, 3);
  REQUIRE(sub.size() == 3);
  REQUIRE(sub[0] == 20);
  REQUIRE(sub[1] == 99);
  REQUIRE(sub[2] == 40);

  // Iterators and range-for
  std::uint32_t sum = 0;
  for (auto val : const_row) {
    sum += val;
  }
  REQUIRE(sum == (10 + 20 + 99 + 40 + 50));
}

TEST_CASE("span2d Plane indexes stride-backed 2D planes and extracts rows") {
  constexpr int width = 4;
  constexpr int height = 3;
  constexpr int stride_elements = 8;
  std::vector<std::uint16_t> buffer(stride_elements * height, 0);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      buffer[static_cast<std::size_t>(y * stride_elements + x)] = static_cast<std::uint16_t>(y * 100 + x);
    }
  }

  span2d::Plane<std::uint16_t> plane(buffer.data(), width, height, stride_elements);

  STATIC_REQUIRE(sizeof(span2d::Plane<std::uint16_t>) == 24);
  STATIC_REQUIRE(sizeof(span2d::Plane<const std::uint16_t>) == 24);

  REQUIRE(plane.width() == width);
  REQUIRE(plane.height() == height);
  REQUIRE(plane.stride() == stride_elements);
  REQUIRE(plane.stride_bytes() == stride_elements * sizeof(std::uint16_t));
  REQUIRE_FALSE(plane.empty());

  // 2D indexing
  REQUIRE(plane(0, 0) == 0);
  REQUIRE(plane(1, 2) == 102);
  REQUIRE(plane(2, 3) == 203);

  // Row extraction
  auto r1 = plane.row(1);
  REQUIRE(r1.size() == 4);
  REQUIRE(r1[2] == 102);

  // Row pointer
  REQUIRE(plane.row_ptr(2)[3] == 203);

  // Mutability
  plane(1, 2) = 777;
  REQUIRE(buffer[stride_elements + 2] == 777);

  // Const conversion
  span2d::Plane<const std::uint16_t> const_plane = plane;
  REQUIRE(const_plane(1, 2) == 777);
  REQUIRE(const_plane.row(1)[2] == 777);

  // Subplane ROI
  auto sub = plane.subplane(1, 1, 2, 2);
  REQUIRE(sub.width() == 2);
  REQUIRE(sub.height() == 2);
  REQUIRE(sub.stride() == stride_elements);
  REQUIRE(sub(0, 0) == 101);
  REQUIRE(sub(0, 1) == 777);
  REQUIRE(sub(1, 1) == 202);
}

TEST_CASE("span2d RowCursor steps through scanlines and supports relative peeking") {
  constexpr int width = 4;
  constexpr int height = 3;
  constexpr int stride_elements = 8;
  std::vector<std::int32_t> buffer(stride_elements * height, 0);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      buffer[static_cast<std::size_t>(y * stride_elements + x)] = y * 10 + x;
    }
  }

  span2d::Plane<std::int32_t> plane(buffer.data(), width, height, stride_elements);

  STATIC_REQUIRE(sizeof(span2d::RowCursor<std::int32_t>) == 16);
  STATIC_REQUIRE(sizeof(span2d::RowCursor<const std::int32_t>) == 16);

  auto cur = plane.cursor();
  REQUIRE(cur.ptr() == buffer.data());
  REQUIRE(cur.width() == width);
  REQUIRE(cur.stride() == stride_elements);

  REQUIRE((*cur)[0] == 0);
  REQUIRE((*cur)[3] == 3);

  // Relative row peek
  REQUIRE(cur[1][2] == 12);
  REQUIRE(cur[2][1] == 21);

  // Stepping
  ++cur;
  REQUIRE((*cur)[0] == 10);
  REQUIRE((*cur)[2] == 12);

  cur += 1;
  REQUIRE((*cur)[0] == 20);

  --cur;
  REQUIRE((*cur)[0] == 10);

  cur -= 1;
  REQUIRE((*cur)[0] == 0);

  // Range-based for over Plane
  int row_idx = 0;
  for (span2d::Row<std::int32_t> r : plane) {
    REQUIRE(r[0] == row_idx * 10);
    ++row_idx;
  }
  REQUIRE(row_idx == 3);
}

TEST_CASE("span2d factory functions and ds namespace aliases work properly") {
  std::vector<std::uint8_t> buffer(16, 42);
  auto p = ds::make_plane(buffer.data(), 4, 4, 4);
  STATIC_REQUIRE(std::is_same_v<decltype(p), ds::Plane<std::uint8_t>>);

  REQUIRE(p(2, 2) == 42);

  auto r = ds::make_row(buffer.data(), buffer.size());
  STATIC_REQUIRE(std::is_same_v<decltype(r), ds::Row<std::uint8_t>>);
  REQUIRE(r[5] == 42);
}
