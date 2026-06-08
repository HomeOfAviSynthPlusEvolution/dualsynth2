#include <catch2/catch_test_macros.hpp>
#include <dualsynth/format.hpp>

TEST_CASE("Video format accepts modern planar formats") {
  const ds::VideoFormat format{
    ds::ColorFamily::Yuv,
    ds::SampleFormat::UInt16,
    3,
    1,
    1
  };

  REQUIRE(ds::is_supported_video_format(format).has_value());
}

TEST_CASE("Video format rejects logical 10-bit") {
  const ds::VideoFormat format{
    ds::ColorFamily::Yuv,
    ds::SampleFormat::UInt10,
    3,
    1,
    1
  };

  const auto result = ds::is_supported_video_format(format);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error().code == ds::ErrorCode::UnsupportedFormat);
}

TEST_CASE("Audio format stores sample type and channel count") {
  const ds::AudioFormat format{
    ds::AudioSampleFormat::Float32,
    48000,
    2
  };

  REQUIRE(format.sample_format == ds::AudioSampleFormat::Float32);
  REQUIRE(format.sample_rate == 48000);
  REQUIRE(format.channels == 2);
}
