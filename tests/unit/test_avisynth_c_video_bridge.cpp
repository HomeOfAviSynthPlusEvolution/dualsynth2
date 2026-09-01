#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <avisynth_c.h>
#include <dualsynth/avisynth/c/video_bridge.hpp>

TEST_CASE("AviSynth C format mapping matches standard formats", "[avisynth_c]") {
  AVS_VideoInfo vi{};
  vi.width = 1920;
  vi.height = 1080;
  vi.fps_numerator = 24;
  vi.fps_denominator = 1;
  vi.num_frames = 100;
  vi.pixel_type = AVS_CS_Y8;

  const auto format = ds::avisynth::c::make_video_format(vi);
  REQUIRE(format.has_value());
  CHECK(format.value().color_family == ds::ColorFamily::Gray);
  CHECK(format.value().sample_format == ds::SampleFormat::UInt8);
  CHECK(format.value().plane_count == 1);

  const auto back_vi = ds::avisynth::c::make_video_info(
    ds::VideoOutputInfo{1920, 1080, 100, format.value(), ds::FrameRate{24, 1}}
  );
  CHECK(back_vi.pixel_type == AVS_CS_Y8);
  CHECK(back_vi.width == 1920);
  CHECK(back_vi.height == 1080);
}

TEST_CASE("AvisynthCValueParamSource reads 64-bit aware integers, floats, booleans, and arrays", "[avisynth_c]") {
  const ds::FilterDescriptor descriptor{
    "TestFilter",
    std::vector<ds::ParamSpec>{
      ds::ParamSpec{"clip", ds::ParamType::Clip, ds::ParamValue{}, true},
      ds::ParamSpec{"req_int", ds::ParamType::Integer, ds::ParamValue{}, true},
      ds::ParamSpec{"opt_float", ds::ParamType::Float, ds::ParamValue{2.5}, false},
      ds::ParamSpec{"opt_bool", ds::ParamType::Boolean, ds::ParamValue{true}, false},
      ds::ParamSpec{"str_val", ds::ParamType::String, ds::ParamValue{std::string("default")}, false},
      ds::ParamSpec{"arr_int", ds::ParamType::Integer, ds::ParamValue{std::vector<std::int64_t>{}}, false, true}
    }
  };

  AVS_Value elements[6];
  // 0: clip (skipped in scalar parsing)
  elements[0].type = 'c';
  elements[0].d.clip = nullptr;
  // 1: req_int
  elements[1].type = 'i';
  elements[1].d.integer = 42;
  // 2: opt_float
  elements[2].type = 'f';
  elements[2].d.floating_pt = 3.14f;
  // 3: opt_bool
  elements[3].type = 'b';
  elements[3].d.boolean = 1;
  // 4: str_val
  elements[4].type = 's';
  elements[4].d.string = "hello";
  // 5: arr_int as string fallback "1, 2, 3"
  elements[5].type = 's';
  elements[5].d.string = "1, 2, 3";

  AVS_Value args;
  args.type = 'a';
  args.array_size = 6;
  args.d.array = elements;

  const auto result = ds::avisynth::c::read_params(args, descriptor);
  REQUIRE(result.has_value());
  const auto& values = result.value();
  CHECK(values.get_int("req_int", 0).value() == 42);
  CHECK(values.get_double("opt_float", 0.0).value() == Catch::Approx(3.14));
  CHECK(values.get_bool("opt_bool", false).value() == true);
  CHECK(values.get_string("str_val", "").value() == "hello");
  CHECK(values.get_int_array("arr_int", {}).value() == std::vector<std::int64_t>{1, 2, 3});
}

TEST_CASE("AviSynth C bridge MT mode and plane id mappings", "[avisynth_c]") {
  CHECK(ds::avisynth::c::host_mt_mode(ds::avisynth::c::MtMode::NiceFilter) == AVS_MT_NICE_FILTER);
  CHECK(ds::avisynth::c::host_mt_mode(ds::avisynth::c::MtMode::MultiInstance) == AVS_MT_MULTI_INSTANCE);
  CHECK(ds::avisynth::c::host_mt_mode(ds::avisynth::c::MtMode::Serialized) == AVS_MT_SERIALIZED);

  const ds::VideoFormat yuv{ds::ColorFamily::Yuv, ds::SampleFormat::UInt8, 3, 1, 1};
  CHECK(ds::avisynth::c::plane_id(yuv, 0) == AVS_PLANAR_Y);
  CHECK(ds::avisynth::c::plane_id(yuv, 1) == AVS_PLANAR_U);
  CHECK(ds::avisynth::c::plane_id(yuv, 2) == AVS_PLANAR_V);

  const ds::VideoFormat rgb{ds::ColorFamily::Rgb, ds::SampleFormat::UInt8, 3, 0, 0};
  CHECK(ds::avisynth::c::plane_id(rgb, 0) == AVS_PLANAR_R);
  CHECK(ds::avisynth::c::plane_id(rgb, 1) == AVS_PLANAR_G);
  CHECK(ds::avisynth::c::plane_id(rgb, 2) == AVS_PLANAR_B);
}

TEST_CASE("AviSynth C bridge output origin matching", "[avisynth_c]") {
  const ds::VideoFormat format{ds::ColorFamily::Gray, ds::SampleFormat::UInt8, 1, 0, 0};
  const ds::VideoOutputInfo output{640, 480, 100, format, ds::FrameRate{24, 1}};
  const std::vector<ds::VideoInputInfo> inputs{
    ds::VideoInputInfo{640, 480, 100, format, ds::FrameRate{24, 1}},
    ds::VideoInputInfo{320, 240, 100, format, ds::FrameRate{24, 1}}
  };

  CHECK(ds::avisynth::c::output_origin_matches(
    ds::OutputOrigin::take_from_input(0), output, ds::Span<const ds::VideoInputInfo>(inputs.data(), inputs.size())
  ));

  CHECK_FALSE(ds::avisynth::c::output_origin_matches(
    ds::OutputOrigin::take_from_input(1), output, ds::Span<const ds::VideoInputInfo>(inputs.data(), inputs.size())
  ));

  CHECK(ds::avisynth::c::output_origin_matches(
    ds::OutputOrigin::fresh(), output, ds::Span<const ds::VideoInputInfo>(inputs.data(), inputs.size())
  ));
}
