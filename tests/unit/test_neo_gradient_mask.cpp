#include <catch2/catch_test_macros.hpp>
#include <dualsynth/reference/neo_gradient_mask.hpp>
#include <array>
#include <span>
#include <vector>

namespace {

ds::MutableVideoFrameView make_rgb8_frame(
  unsigned char* r,
  unsigned char* g,
  unsigned char* b,
  int width,
  int height
) {
  return ds::MutableVideoFrameView{
    ds::VideoFormat{ds::ColorFamily::Rgb, ds::SampleFormat::UInt8, 3, 0, 0},
    3,
    std::array<ds::MutablePlaneView, 4>{
      ds::MutablePlaneView{r, width, width, height},
      ds::MutablePlaneView{g, width, width, height},
      ds::MutablePlaneView{b, width, width, height},
      ds::MutablePlaneView{}
    }
  };
}

} // namespace

TEST_CASE("NeoGradientMask initializes RGB source output") {
  const ds::ParamValues params{
    std::vector<ds::ParamEntry>{
      ds::ParamEntry{"width", ds::ParamValue{4}},
      ds::ParamEntry{"height", ds::ParamValue{2}},
      ds::ParamEntry{"depth", ds::ParamValue{16}}
    }
  };
  ds::VideoInitContext context{std::span<const ds::VideoInputInfo>{}, &params};

  const auto result = ds::reference::NeoGradientMask::init(context);

  REQUIRE(result.has_value());
  REQUIRE(result.value().output.width == 4);
  REQUIRE(result.value().output.height == 2);
  REQUIRE(result.value().output.num_frames == 10000);
  REQUIRE(result.value().output.format == ds::VideoFormat{ds::ColorFamily::Rgb, ds::SampleFormat::UInt16, 3, 0, 0});
  REQUIRE(result.value().output.fps == ds::FrameRate{30000, 1001});
}

TEST_CASE("NeoGradientMask renders RGB8 planes") {
  const ds::ParamValues params{
    std::vector<ds::ParamEntry>{
      ds::ParamEntry{"width", ds::ParamValue{4}},
      ds::ParamEntry{"height", ds::ParamValue{1}},
      ds::ParamEntry{"color", ds::ParamValue{0x99ccff}},
      ds::ParamEntry{"depth", ds::ParamValue{8}}
    }
  };
  std::array<unsigned char, 4> r{};
  std::array<unsigned char, 4> g{};
  std::array<unsigned char, 4> b{};

  const auto result = ds::reference::NeoGradientMask::process_source(
    0,
    params,
    make_rgb8_frame(r.data(), g.data(), b.data(), 4, 1)
  );

  REQUIRE(result.has_value());
  REQUIRE(r[0] < r[3]);
  REQUIRE(g[0] < g[3]);
  REQUIRE(b[0] < b[3]);
  REQUIRE(b[0] > r[0]);
}
