#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <dualsynth/reference/video_filters.hpp>

TEST_CASE("Reference video identity copies uint8 planes") {
  const std::array<unsigned char, 4> src_storage{1, 2, 3, 4};
  std::array<unsigned char, 4> dst_storage{};

  const ds::PlaneSpan<const unsigned char> src(src_storage.data(), 2, 2, 2);
  ds::PlaneSpan<unsigned char> dst(dst_storage.data(), 2, 2, 2);

  ds::reference::copy_plane(src, dst);

  REQUIRE(dst_storage == src_storage);
}

TEST_CASE("Reference video invert handles uint8 and uint16 ranges") {
  const std::array<unsigned char, 4> u8_src{0, 1, 127, 255};
  std::array<unsigned char, 4> u8_dst{};

  ds::reference::invert_plane(
    ds::PlaneSpan<const unsigned char>(u8_src.data(), 4, 1, 4),
    ds::PlaneSpan<unsigned char>(u8_dst.data(), 4, 1, 4)
  );

  REQUIRE(u8_dst == std::array<unsigned char, 4>{255, 254, 128, 0});

  const std::array<unsigned short, 3> u16_src{0, 1024, 65535};
  std::array<unsigned short, 3> u16_dst{};

  ds::reference::invert_plane(
    ds::PlaneSpan<const unsigned short>(u16_src.data(), 3, 1, 3 * sizeof(unsigned short)),
    ds::PlaneSpan<unsigned short>(u16_dst.data(), 3, 1, 3 * sizeof(unsigned short))
  );

  REQUIRE(u16_dst == std::array<unsigned short, 3>{65535, 64511, 0});
}

TEST_CASE("Reference video invert handles float normalized range") {
  const std::array<float, 3> src_storage{0.0F, 0.25F, 1.0F};
  std::array<float, 3> dst_storage{};

  ds::reference::invert_plane(
    ds::PlaneSpan<const float>(src_storage.data(), 3, 1, 3 * sizeof(float)),
    ds::PlaneSpan<float>(dst_storage.data(), 3, 1, 3 * sizeof(float))
  );

  REQUIRE(dst_storage[0] == Catch::Approx(1.0F));
  REQUIRE(dst_storage[1] == Catch::Approx(0.75F));
  REQUIRE(dst_storage[2] == Catch::Approx(0.0F));
}

TEST_CASE("Reference video transpose swaps dimensions and double transpose restores data") {
  const std::array<unsigned char, 6> src_storage{
    1, 2, 3,
    4, 5, 6
  };
  std::array<unsigned char, 6> transposed_storage{};
  std::array<unsigned char, 6> restored_storage{};

  ds::reference::transpose_plane(
    ds::PlaneSpan<const unsigned char>(src_storage.data(), 3, 2, 3),
    ds::PlaneSpan<unsigned char>(transposed_storage.data(), 2, 3, 2)
  );

  REQUIRE(transposed_storage == std::array<unsigned char, 6>{
    1, 4,
    2, 5,
    3, 6
  });

  ds::reference::transpose_plane(
    ds::PlaneSpan<const unsigned char>(transposed_storage.data(), 2, 3, 2),
    ds::PlaneSpan<unsigned char>(restored_storage.data(), 3, 2, 3)
  );

  REQUIRE(restored_storage == src_storage);
}
