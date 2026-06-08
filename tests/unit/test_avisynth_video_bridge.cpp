#include <dualsynth/avisynth/video_bridge.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("AviSynth video bridge maps middle bit-depth planar formats") {
  REQUIRE(ds::avisynth::pixel_type(
    ds::VideoFormat{ds::ColorFamily::Gray, ds::SampleFormat::UInt10, 1, 0, 0}
  ) == VideoInfo::CS_Y10);
  REQUIRE(ds::avisynth::pixel_type(
    ds::VideoFormat{ds::ColorFamily::Gray, ds::SampleFormat::UInt12, 1, 0, 0}
  ) == VideoInfo::CS_Y12);
  REQUIRE(ds::avisynth::pixel_type(
    ds::VideoFormat{ds::ColorFamily::Gray, ds::SampleFormat::UInt14, 1, 0, 0}
  ) == VideoInfo::CS_Y14);

  REQUIRE(ds::avisynth::pixel_type(
    ds::VideoFormat{ds::ColorFamily::Rgb, ds::SampleFormat::UInt10, 3, 0, 0}
  ) == VideoInfo::CS_RGBP10);
  REQUIRE(ds::avisynth::pixel_type(
    ds::VideoFormat{ds::ColorFamily::Rgb, ds::SampleFormat::UInt12, 3, 0, 0}
  ) == VideoInfo::CS_RGBP12);
  REQUIRE(ds::avisynth::pixel_type(
    ds::VideoFormat{ds::ColorFamily::Rgb, ds::SampleFormat::UInt14, 3, 0, 0}
  ) == VideoInfo::CS_RGBP14);
}
