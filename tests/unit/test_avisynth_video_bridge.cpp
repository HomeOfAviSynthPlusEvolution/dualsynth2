#include <dualsynth/avisynth/video_bridge.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace {

struct FakeAvisynthSource {
  using Scalar = std::variant<std::int64_t, double, bool, std::string>;

  std::map<int, Scalar> scalars;
  std::map<int, std::vector<std::int64_t>> int_arrays;
  std::map<int, std::vector<double>> float_arrays;
  std::map<int, std::vector<bool>> bool_arrays;
  std::map<int, std::vector<std::string>> string_arrays;

  bool defined(int index) const {
    return scalars.contains(index) ||
      int_arrays.contains(index) ||
      float_arrays.contains(index) ||
      bool_arrays.contains(index) ||
      string_arrays.contains(index);
  }

  std::int64_t as_int(int index) const {
    return std::get<std::int64_t>(scalars.at(index));
  }

  double as_float(int index) const {
    return std::get<double>(scalars.at(index));
  }

  bool as_bool(int index) const {
    return std::get<bool>(scalars.at(index));
  }

  std::string as_string(int index) const {
    return std::get<std::string>(scalars.at(index));
  }

  std::vector<std::int64_t> as_int_array(int index) const {
    return int_arrays.at(index);
  }

  std::vector<double> as_float_array(int index) const {
    return float_arrays.at(index);
  }

  std::vector<bool> as_bool_array(int index) const {
    return bool_arrays.at(index);
  }

  std::vector<std::string> as_string_array(int index) const {
    return string_arrays.at(index);
  }
};

} // namespace

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

TEST_CASE("AviSynth parameter reader converts positional host arguments using descriptor metadata") {
  const ds::FilterDescriptor descriptor{
    "Sample",
    std::vector<ds::ParamSpec>{
      ds::ParamSpec{"clip", ds::ParamType::Clip, ds::ParamValue{}, true},
      ds::ParamSpec{"sigma", ds::ParamType::Float, ds::ParamValue{1.0}, false},
      ds::ParamSpec{"planes", ds::ParamType::Integer, ds::ParamValue{std::vector<std::int64_t>{}}, false, true},
      ds::ParamSpec{"enabled", ds::ParamType::Boolean, ds::ParamValue{false}, false},
      ds::ParamSpec{"vs_only", ds::ParamType::Integer, ds::ParamValue{0}, false, false, true, false}
    }
  };
  FakeAvisynthSource source{
    .scalars = {
      {1, 2.5},
      {2, std::string{"0, 1"}},
      {3, true}
    },
    .int_arrays = {
      {4, std::vector<std::int64_t>{0, 2}}
    },
    .float_arrays = {},
    .bool_arrays = {},
    .string_arrays = {}
  };

  const auto result = ds::avisynth::read_params_from_source(source, descriptor);

  REQUIRE(result.has_value());
  const auto& values = result.value();
  REQUIRE(values.get_double("sigma", 0.0).value() == 2.5);
  REQUIRE(values.get_int_array("planes", {}).value() == std::vector<std::int64_t>{0, 2});
  REQUIRE(values.get_bool("enabled", false).value());
  REQUIRE(values.get_int("vs_only", 4).value() == 4);
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
