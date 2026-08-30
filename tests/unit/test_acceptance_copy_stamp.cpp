#include <catch2/catch_test_macros.hpp>
#include "copy_stamp.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

class FailingFrameProvider final : public ds::VideoFrameProvider {
public:
  ds::Result<ds::RequestedVideoFrame> get(int, int) override {
    return ds::Result<ds::RequestedVideoFrame>::failure(
      ds::Error{ds::ErrorCode::InternalError, "provider should not be used"}
    );
  }
};

ds::MutableVideoFrameView make_mutable_gray8_frame(std::uint8_t* data, int width, int height, std::ptrdiff_t stride) {
  return ds::MutableVideoFrameView{
    ds::VideoFormat{ds::ColorFamily::Gray, ds::SampleFormat::UInt8, 1, 0, 0},
    1,
    std::array<ds::MutablePlaneView, 4>{
      ds::MutablePlaneView{data, stride, width, height},
      ds::MutablePlaneView{},
      ds::MutablePlaneView{},
      ds::MutablePlaneView{}
    }
  };
}

} // namespace

TEST_CASE("AcceptanceCopyStamp declares copied output and stamps the copied frame") {
  const ds::VideoInputInfo input{
    2,
    2,
    3,
    ds::VideoFormat{ds::ColorFamily::Gray, ds::SampleFormat::UInt8, 1, 0, 0},
    ds::FrameRate{24, 1}
  };
  std::array<std::uint8_t, 4> dst_storage{1, 2, 3, 4};
  FailingFrameProvider provider;

  const auto init = ds::init_video_filter<ds::acceptance::AcceptanceCopyStamp>(
    std::array<ds::VideoInputInfo, 1>{input}
  );
  ds::VideoProcessContext context{
    1,
    provider,
    make_mutable_gray8_frame(dst_storage.data(), 2, 2, 2)
  };

  const auto process = ds::acceptance::AcceptanceCopyStamp::process(context);

  REQUIRE(ds::acceptance::AcceptanceCopyStamp::output_origin.pixels == ds::OutputPixelPolicy::CopyFromInput);
  REQUIRE(ds::acceptance::AcceptanceCopyStamp::output_origin.pixel_input_index == 0);
  REQUIRE(ds::acceptance::AcceptanceCopyStamp::output_origin.prop_input_index == 0);
  REQUIRE(init.has_value());
  REQUIRE(init.value().output.format == input.format);
  REQUIRE(process.has_value());
  REQUIRE(dst_storage == std::array<std::uint8_t, 4>{255, 2, 3, 4});
}
