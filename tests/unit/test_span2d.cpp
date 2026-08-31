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
  STATIC_REQUIRE_FALSE(span2d::Row<std::uint8_t>::is_restrict);

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
  STATIC_REQUIRE_FALSE(span2d::Plane<std::uint16_t>::is_restrict);

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
  STATIC_REQUIRE_FALSE(span2d::RowCursor<std::int32_t>::is_restrict);

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

TEST_CASE("span2d Restrict views and explicit conversion methods work seamlessly") {
  std::vector<std::uint8_t> buffer = {1, 2, 3, 4, 5, 6, 7, 8};
  span2d::Plane<std::uint8_t> plane(buffer.data(), 4, 2, 4);

  STATIC_REQUIRE(sizeof(span2d::RestrictPlane<std::uint8_t>) == 24);
  STATIC_REQUIRE(sizeof(span2d::RestrictRow<std::uint8_t>) == 16);
  STATIC_REQUIRE(sizeof(span2d::RestrictRowCursor<std::uint8_t>) == 16);

  STATIC_REQUIRE(span2d::RestrictPlane<std::uint8_t>::is_restrict);
  STATIC_REQUIRE(span2d::RestrictRow<std::uint8_t>::is_restrict);
  STATIC_REQUIRE(span2d::RestrictRowCursor<std::uint8_t>::is_restrict);

  // 1. Plane .as_restrict() & .as_unrestricted()
  auto rest_plane = plane.as_restrict();
  STATIC_REQUIRE(std::is_same_v<decltype(rest_plane), span2d::RestrictPlane<std::uint8_t>>);
  REQUIRE(rest_plane(1, 2) == 7);

  auto back_plane = rest_plane.as_unrestricted();
  STATIC_REQUIRE(std::is_same_v<decltype(back_plane), span2d::Plane<std::uint8_t>>);
  REQUIRE(back_plane(1, 2) == 7);

  // 2. Row .as_restrict() & .as_unrestricted()
  auto row = plane.row(1);
  auto rest_row = row.as_restrict();
  STATIC_REQUIRE(std::is_same_v<decltype(rest_row), span2d::RestrictRow<std::uint8_t>>);
  REQUIRE(rest_row[2] == 7);

  auto back_row = rest_row.as_unrestricted();
  STATIC_REQUIRE(std::is_same_v<decltype(back_row), span2d::Row<std::uint8_t>>);
  REQUIRE(back_row[2] == 7);

  // 3. Cursor .as_restrict() & .as_unrestricted()
  auto cursor = plane.cursor();
  auto rest_cur = cursor.as_restrict();
  STATIC_REQUIRE(std::is_same_v<decltype(rest_cur), span2d::RestrictRowCursor<std::uint8_t>>);
  REQUIRE((*rest_cur)[3] == 4);

  auto back_cur = rest_cur.as_unrestricted();
  STATIC_REQUIRE(std::is_same_v<decltype(back_cur), span2d::RowCursor<std::uint8_t>>);
  REQUIRE((*back_cur)[3] == 4);

  // 4. Free conversion functions
  auto p_free_rest = span2d::as_restrict(plane);
  STATIC_REQUIRE(std::is_same_v<decltype(p_free_rest), span2d::RestrictPlane<std::uint8_t>>);

  auto r_free_rest = span2d::as_restrict(row);
  STATIC_REQUIRE(std::is_same_v<decltype(r_free_rest), span2d::RestrictRow<std::uint8_t>>);

  auto c_free_rest = span2d::as_restrict(cursor);
  STATIC_REQUIRE(std::is_same_v<decltype(c_free_rest), span2d::RestrictRowCursor<std::uint8_t>>);

  auto p_free_unrest = span2d::as_unrestricted(rest_plane);
  STATIC_REQUIRE(std::is_same_v<decltype(p_free_unrest), span2d::Plane<std::uint8_t>>);
}

TEST_CASE("span2d factory functions and ds namespace aliases work properly") {
  std::vector<std::uint8_t> buffer(16, 42);

  // Standard safe factory
  auto p = ds::make_plane(buffer.data(), 4, 4, 4);
  STATIC_REQUIRE(std::is_same_v<decltype(p), ds::Plane<std::uint8_t>>);
  REQUIRE(p(2, 2) == 42);

  auto r = ds::make_row(buffer.data(), buffer.size());
  STATIC_REQUIRE(std::is_same_v<decltype(r), ds::Row<std::uint8_t>>);
  REQUIRE(r[5] == 42);

  // Span aliases and container constructors
  ds::Span<std::uint8_t> span_from_vec(buffer);
  REQUIRE(span_from_vec.size() == 16);
  REQUIRE(span_from_vec[0] == 42);

  std::array<int, 4> arr{1, 2, 3, 4};
  ds::Span<const int> span_from_arr(arr);
  REQUIRE(span_from_arr.size() == 4);
  REQUIRE(span_from_arr[2] == 3);

  int c_arr[3] = {10, 20, 30};
  ds::Span<int> span_from_c_arr(c_arr);
  REQUIRE(span_from_c_arr.size() == 3);
  REQUIRE(span_from_c_arr[1] == 20);

  auto span_factory = ds::make_span(buffer.data(), buffer.size());
  STATIC_REQUIRE(std::is_same_v<decltype(span_factory), ds::Span<std::uint8_t>>);

  // Restrict factory
  auto rp = ds::make_restrict_plane(buffer.data(), 4, 4, 4);
  STATIC_REQUIRE(std::is_same_v<decltype(rp), ds::RestrictPlane<std::uint8_t>>);
  REQUIRE(rp(2, 2) == 42);

  auto rr = ds::make_restrict_row(buffer.data(), buffer.size());
  STATIC_REQUIRE(std::is_same_v<decltype(rr), ds::RestrictRow<std::uint8_t>>);
  REQUIRE(rr[5] == 42);
}
