#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <dualsynth/acceptance/temporal_average3.hpp>
#include <dualsynth/mdspan.hpp>
#include <dualsynth/reference/video_filters.hpp>
#include <string>
#include <type_traits>
#include <vector>

namespace {

class SingleFrameProvider final : public ds::VideoFrameProvider {
public:
  explicit SingleFrameProvider(ds::PlaneView2D<const unsigned char> src)
    : src_(src) {}

  ds::Result<ds::RequestedVideoFrame> get(int input_index, int frame_number) override {
    requested_input_ = input_index;
    requested_frame_ = frame_number;
    return ds::Result<ds::RequestedVideoFrame>::success(
      ds::RequestedVideoFrame{input_index, frame_number, src_}
    );
  }

  int requested_input() const {
    return requested_input_;
  }

  int requested_frame() const {
    return requested_frame_;
  }

private:
  ds::PlaneView2D<const unsigned char> src_;
  int requested_input_ = -1;
  int requested_frame_ = -1;
};

} // namespace

TEST_CASE("Reference video identity copies uint8 planes") {
  const std::array<unsigned char, 4> src_storage{1, 2, 3, 4};
  std::array<unsigned char, 4> dst_storage{};

  auto src = ds::make_plane_view(src_storage.data(), 2, 2, 2);
  auto dst = ds::make_plane_view(dst_storage.data(), 2, 2, 2);

  ds::reference::copy_plane(src, dst);

  REQUIRE(dst_storage == src_storage);
}

TEST_CASE("Reference video invert handles uint8 and uint16 ranges") {
  const std::array<unsigned char, 4> u8_src{0, 1, 127, 255};
  std::array<unsigned char, 4> u8_dst{};

  ds::reference::invert_plane(
    ds::make_plane_view(u8_src.data(), 4, 1, 4),
    ds::make_plane_view(u8_dst.data(), 4, 1, 4)
  );

  REQUIRE(u8_dst == std::array<unsigned char, 4>{255, 254, 128, 0});

  const std::array<unsigned short, 3> u16_src{0, 1024, 65535};
  std::array<unsigned short, 3> u16_dst{};

  ds::reference::invert_plane(
    ds::make_plane_view(u16_src.data(), 3, 1, 3 * sizeof(unsigned short)),
    ds::make_plane_view(u16_dst.data(), 3, 1, 3 * sizeof(unsigned short))
  );

  REQUIRE(u16_dst == std::array<unsigned short, 3>{65535, 64511, 0});
}

TEST_CASE("Reference video invert handles float normalized range") {
  const std::array<float, 3> src_storage{0.0F, 0.25F, 1.0F};
  std::array<float, 3> dst_storage{};

  ds::reference::invert_plane(
    ds::make_plane_view(src_storage.data(), 3, 1, 3 * sizeof(float)),
    ds::make_plane_view(dst_storage.data(), 3, 1, 3 * sizeof(float))
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
    ds::make_plane_view(src_storage.data(), 3, 2, 3),
    ds::make_plane_view(transposed_storage.data(), 2, 3, 2)
  );

  REQUIRE(transposed_storage == std::array<unsigned char, 6>{
    1, 4,
    2, 5,
    3, 6
  });

  ds::reference::transpose_plane(
    ds::make_plane_view(static_cast<const unsigned char*>(transposed_storage.data()), 2, 3, 2),
    ds::make_plane_view(restored_storage.data(), 3, 2, 3)
  );

  REQUIRE(restored_storage == src_storage);
}

TEST_CASE("Reference video descriptors expose metadata and initialize output info") {
  const ds::VideoFormat input_format{
    ds::ColorFamily::Rgb,
    ds::SampleFormat::UInt16,
    3,
    0,
    0
  };
  const ds::FrameRate input_fps{30000, 1001};
  std::vector<ds::VideoInputInfo> input{ds::VideoInputInfo{13, 7, 3, input_format, input_fps}};

  const auto identity_init = ds::init_video_filter<ds::reference::VideoIdentity>(input);
  const auto transpose_init = ds::init_video_filter<ds::reference::VideoTranspose>(input);

  REQUIRE(std::string(ds::reference::VideoIdentity::name) == "VideoIdentity");
  REQUIRE(ds::reference::VideoIdentity::input_count == 1);
  REQUIRE(identity_init.has_value());
  REQUIRE(identity_init.value().output.width == 13);
  REQUIRE(identity_init.value().output.height == 7);
  REQUIRE(identity_init.value().output.num_frames == 3);
  REQUIRE(identity_init.value().output.format == input_format);
  REQUIRE(identity_init.value().output.fps == input_fps);

  REQUIRE(std::string(ds::reference::VideoTranspose::name) == "VideoTranspose");
  REQUIRE(ds::reference::VideoTranspose::input_count == 1);
  REQUIRE(transpose_init.has_value());
  REQUIRE(transpose_init.value().output.width == 7);
  REQUIRE(transpose_init.value().output.height == 13);
  REQUIRE(transpose_init.value().output.num_frames == 3);
  REQUIRE(transpose_init.value().output.format == input_format);
  REQUIRE(transpose_init.value().output.fps == input_fps);
}

TEST_CASE("VideoTransposeBridge exposes host binding metadata") {
  using Bridge = ds::reference::VideoTransposeBridge;

  REQUIRE((std::is_same_v<Bridge::Core, ds::reference::VideoTranspose>));
  REQUIRE(std::string(Bridge::vs_name) == "VideoTranspose");
  REQUIRE(std::string(Bridge::vs_signature) == "clip:vnode;");
  REQUIRE(Bridge::vs_input_names.size() == 1);
  REQUIRE(std::string(Bridge::vs_input_names[0]) == "clip");
  REQUIRE(std::string(Bridge::avs_name) == "DSVideoTranspose");
  REQUIRE(std::string(Bridge::avs_signature) == "c");
  REQUIRE(Bridge::parity_source_index == 0);
  REQUIRE(Bridge::forward_audio == true);
}

TEST_CASE("VideoIdentityBridge exposes host binding metadata") {
  using Bridge = ds::reference::VideoIdentityBridge;

  REQUIRE((std::is_same_v<Bridge::Core, ds::reference::VideoIdentity>));
  REQUIRE(std::string(Bridge::vs_name) == "VideoIdentity");
  REQUIRE(std::string(Bridge::vs_signature) == "clip:vnode;");
  REQUIRE(Bridge::vs_input_names.size() == 1);
  REQUIRE(std::string(Bridge::vs_input_names[0]) == "clip");
  REQUIRE(std::string(Bridge::avs_name) == "DSVideoIdentity");
  REQUIRE(std::string(Bridge::avs_signature) == "c");
  REQUIRE(Bridge::parity_source_index == 0);
  REQUIRE(Bridge::forward_audio == true);
}

TEST_CASE("VideoInvertBridge exposes host binding metadata") {
  using Bridge = ds::reference::VideoInvertBridge;

  REQUIRE((std::is_same_v<Bridge::Core, ds::reference::VideoInvert>));
  REQUIRE(std::string(Bridge::vs_name) == "VideoInvert");
  REQUIRE(std::string(Bridge::vs_signature) == "clip:vnode;");
  REQUIRE(Bridge::vs_input_names.size() == 1);
  REQUIRE(std::string(Bridge::vs_input_names[0]) == "clip");
  REQUIRE(std::string(Bridge::avs_name) == "DSVideoInvert");
  REQUIRE(std::string(Bridge::avs_signature) == "c");
  REQUIRE(Bridge::parity_source_index == 0);
  REQUIRE(Bridge::forward_audio == true);
}

TEST_CASE("Video filter dispatch helper requests and processes through descriptors") {
  std::vector<ds::VideoFrameRequest> requests;

  const auto request_result = ds::request_video_filter<ds::reference::VideoInvert>(4, requests);

  REQUIRE(request_result.has_value());
  REQUIRE(requests.size() == 1);
  REQUIRE(requests[0].input_index == 0);
  REQUIRE(requests[0].frame_number == 4);

  const std::array<unsigned char, 4> src_storage{0, 10, 127, 255};
  std::array<unsigned char, 4> dst_storage{};
  SingleFrameProvider provider(ds::make_plane_view(src_storage.data(), 4, 1, 4));

  const auto process_result = ds::process_video_filter<ds::reference::VideoInvert>(
    4,
    provider,
    ds::make_plane_view(dst_storage.data(), 4, 1, 4)
  );

  REQUIRE(process_result.has_value());
  REQUIRE(provider.requested_input() == 0);
  REQUIRE(provider.requested_frame() == 4);
  REQUIRE(dst_storage == std::array<unsigned char, 4>{255, 245, 128, 0});
}

TEST_CASE("Video input info helper follows descriptor input count") {
  std::vector<ds::VideoInputInfo> inputs{
    ds::VideoInputInfo{16, 9, 3},
    ds::VideoInputInfo{16, 9, 3},
    ds::VideoInputInfo{16, 9, 3}
  };

  const auto collected = ds::collect_video_input_infos<ds::acceptance::AcceptanceTemporalAverage3>(inputs);

  REQUIRE(collected.has_value());
  REQUIRE(collected.value().size() == 3);
  REQUIRE(collected.value()[0].width == 16);
  REQUIRE(collected.value()[1].height == 9);
  REQUIRE(collected.value()[2].num_frames == 3);
}

TEST_CASE("Video input info helper rejects the wrong input count") {
  std::vector<ds::VideoInputInfo> inputs{ds::VideoInputInfo{16, 9, 3}};

  const auto collected = ds::collect_video_input_infos<ds::acceptance::AcceptanceTemporalAverage3>(inputs);

  REQUIRE(!collected.has_value());
  REQUIRE(collected.error().code == ds::ErrorCode::InvalidArgument);
}
