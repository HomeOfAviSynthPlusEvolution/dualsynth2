#include <dualsynth/avisynth/video_bridge.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>

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

TEST_CASE("AviSynth video bridge maps YUV 420 422 and 444 planar formats") {
  struct Case {
    ds::SampleFormat sample_format;
    int subsampling_w;
    int subsampling_h;
    int pixel_type;
  };

  constexpr std::array cases{
    Case{ds::SampleFormat::UInt8, 0, 0, VideoInfo::CS_YV24},
    Case{ds::SampleFormat::UInt10, 0, 0, VideoInfo::CS_YUV444P10},
    Case{ds::SampleFormat::UInt12, 0, 0, VideoInfo::CS_YUV444P12},
    Case{ds::SampleFormat::UInt14, 0, 0, VideoInfo::CS_YUV444P14},
    Case{ds::SampleFormat::UInt16, 0, 0, VideoInfo::CS_YUV444P16},
    Case{ds::SampleFormat::Float32, 0, 0, VideoInfo::CS_YUV444PS},

    Case{ds::SampleFormat::UInt8, 1, 0, VideoInfo::CS_YV16},
    Case{ds::SampleFormat::UInt10, 1, 0, VideoInfo::CS_YUV422P10},
    Case{ds::SampleFormat::UInt12, 1, 0, VideoInfo::CS_YUV422P12},
    Case{ds::SampleFormat::UInt14, 1, 0, VideoInfo::CS_YUV422P14},
    Case{ds::SampleFormat::UInt16, 1, 0, VideoInfo::CS_YUV422P16},
    Case{ds::SampleFormat::Float32, 1, 0, VideoInfo::CS_YUV422PS},

    Case{ds::SampleFormat::UInt8, 1, 1, VideoInfo::CS_YV12},
    Case{ds::SampleFormat::UInt10, 1, 1, VideoInfo::CS_YUV420P10},
    Case{ds::SampleFormat::UInt12, 1, 1, VideoInfo::CS_YUV420P12},
    Case{ds::SampleFormat::UInt14, 1, 1, VideoInfo::CS_YUV420P14},
    Case{ds::SampleFormat::UInt16, 1, 1, VideoInfo::CS_YUV420P16},
    Case{ds::SampleFormat::Float32, 1, 1, VideoInfo::CS_YUV420PS},
  };

  for (const auto& item : cases) {
    REQUIRE(ds::avisynth::pixel_type(
      ds::VideoFormat{
        ds::ColorFamily::Yuv,
        item.sample_format,
        3,
        item.subsampling_w,
        item.subsampling_h
      }
    ) == item.pixel_type);
  }
}

TEST_CASE("AviSynth video bridge maps planar RGBA formats") {
  struct Case {
    ds::SampleFormat sample_format;
    int pixel_type;
  };

  constexpr std::array cases{
    Case{ds::SampleFormat::UInt8, VideoInfo::CS_RGBAP},
    Case{ds::SampleFormat::UInt10, VideoInfo::CS_RGBAP10},
    Case{ds::SampleFormat::UInt12, VideoInfo::CS_RGBAP12},
    Case{ds::SampleFormat::UInt14, VideoInfo::CS_RGBAP14},
    Case{ds::SampleFormat::UInt16, VideoInfo::CS_RGBAP16},
    Case{ds::SampleFormat::Float32, VideoInfo::CS_RGBAPS},
  };

  for (const auto& item : cases) {
    REQUIRE(ds::avisynth::pixel_type(
      ds::VideoFormat{
        ds::ColorFamily::Rgb,
        item.sample_format,
        4,
        0,
        0
      }
    ) == item.pixel_type);
  }
}

TEST_CASE("AviSynth video bridge maps planar YUVA formats") {
  struct Case {
    ds::SampleFormat sample_format;
    int subsampling_w;
    int subsampling_h;
    int pixel_type;
  };

  constexpr std::array cases{
    Case{ds::SampleFormat::UInt8, 0, 0, VideoInfo::CS_YUVA444},
    Case{ds::SampleFormat::UInt10, 0, 0, VideoInfo::CS_YUVA444P10},
    Case{ds::SampleFormat::UInt12, 0, 0, VideoInfo::CS_YUVA444P12},
    Case{ds::SampleFormat::UInt14, 0, 0, VideoInfo::CS_YUVA444P14},
    Case{ds::SampleFormat::UInt16, 0, 0, VideoInfo::CS_YUVA444P16},
    Case{ds::SampleFormat::Float32, 0, 0, VideoInfo::CS_YUVA444PS},

    Case{ds::SampleFormat::UInt8, 1, 0, VideoInfo::CS_YUVA422},
    Case{ds::SampleFormat::UInt10, 1, 0, VideoInfo::CS_YUVA422P10},
    Case{ds::SampleFormat::UInt12, 1, 0, VideoInfo::CS_YUVA422P12},
    Case{ds::SampleFormat::UInt14, 1, 0, VideoInfo::CS_YUVA422P14},
    Case{ds::SampleFormat::UInt16, 1, 0, VideoInfo::CS_YUVA422P16},
    Case{ds::SampleFormat::Float32, 1, 0, VideoInfo::CS_YUVA422PS},

    Case{ds::SampleFormat::UInt8, 1, 1, VideoInfo::CS_YUVA420},
    Case{ds::SampleFormat::UInt10, 1, 1, VideoInfo::CS_YUVA420P10},
    Case{ds::SampleFormat::UInt12, 1, 1, VideoInfo::CS_YUVA420P12},
    Case{ds::SampleFormat::UInt14, 1, 1, VideoInfo::CS_YUVA420P14},
    Case{ds::SampleFormat::UInt16, 1, 1, VideoInfo::CS_YUVA420P16},
    Case{ds::SampleFormat::Float32, 1, 1, VideoInfo::CS_YUVA420PS},
  };

  for (const auto& item : cases) {
    REQUIRE(ds::avisynth::pixel_type(
      ds::VideoFormat{
        ds::ColorFamily::Yuv,
        item.sample_format,
        4,
        item.subsampling_w,
        item.subsampling_h
      }
    ) == item.pixel_type);
  }
}
