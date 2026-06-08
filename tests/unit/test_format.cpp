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

TEST_CASE("Video format accepts middle bit-depth planar formats") {
  const ds::VideoFormat format{
    ds::ColorFamily::Yuv,
    ds::SampleFormat::UInt10,
    3,
    1,
    1
  };

  const auto result = ds::is_supported_video_format(format);
  REQUIRE(result.has_value());
}

TEST_CASE("Sample format maps modern storage depths") {
  REQUIRE(ds::sample_format_from_depth(false, 8).value() == ds::SampleFormat::UInt8);
  REQUIRE(ds::sample_format_from_depth(false, 16).value() == ds::SampleFormat::UInt16);
  REQUIRE(ds::sample_format_from_depth(true, 32).value() == ds::SampleFormat::Float32);
}

TEST_CASE("Sample format maps middle bit-depth storage formats") {
  REQUIRE(ds::sample_format_from_depth(false, 10).value() == ds::SampleFormat::UInt10);
  REQUIRE(ds::sample_format_from_depth(false, 12).value() == ds::SampleFormat::UInt12);
  REQUIRE(ds::sample_format_from_depth(false, 14).value() == ds::SampleFormat::UInt14);
}

TEST_CASE("Video format can be built from host format components") {
  const auto format = ds::make_video_format(ds::ColorFamily::Rgb, false, 16, 3, 0, 0);

  REQUIRE(format.has_value());
  REQUIRE(format.value().color_family == ds::ColorFamily::Rgb);
  REQUIRE(format.value().sample_format == ds::SampleFormat::UInt16);
  REQUIRE(format.value().plane_count == 3);
  REQUIRE(format.value().subsampling_w == 0);
  REQUIRE(format.value().subsampling_h == 0);
}

TEST_CASE("Sample format reports byte width") {
  REQUIRE(ds::bytes_per_sample(ds::SampleFormat::UInt8) == 1);
  REQUIRE(ds::bytes_per_sample(ds::SampleFormat::UInt10) == 2);
  REQUIRE(ds::bytes_per_sample(ds::SampleFormat::UInt12) == 2);
  REQUIRE(ds::bytes_per_sample(ds::SampleFormat::UInt14) == 2);
  REQUIRE(ds::bytes_per_sample(ds::SampleFormat::UInt16) == 2);
  REQUIRE(ds::bytes_per_sample(ds::SampleFormat::Float32) == 4);
}

TEST_CASE("Sample format reports bit depth") {
  REQUIRE(ds::bits_per_sample(ds::SampleFormat::UInt8) == 8);
  REQUIRE(ds::bits_per_sample(ds::SampleFormat::UInt10) == 10);
  REQUIRE(ds::bits_per_sample(ds::SampleFormat::UInt12) == 12);
  REQUIRE(ds::bits_per_sample(ds::SampleFormat::UInt14) == 14);
  REQUIRE(ds::bits_per_sample(ds::SampleFormat::UInt16) == 16);
  REQUIRE(ds::bits_per_sample(ds::SampleFormat::Float32) == 32);
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
