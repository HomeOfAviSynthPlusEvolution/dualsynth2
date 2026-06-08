#include <array>
#include <catch2/catch_test_macros.hpp>
#include <dualsynth/acceptance/temporal_average3.hpp>
#include <string>
#include <type_traits>
#include <vector>

namespace {

class FakeFrameProvider final : public ds::VideoFrameProvider {
public:
  FakeFrameProvider(
    ds::PlaneSpan<const unsigned char> a,
    ds::PlaneSpan<const unsigned char> b,
    ds::PlaneSpan<const unsigned char> c
  ) : frames_{a, b, c} {}

  ds::Result<ds::RequestedVideoFrame> get(int input_index, int frame_number) override {
    requested_inputs_[static_cast<std::size_t>(request_count_)] = input_index;
    requested_frames_[static_cast<std::size_t>(request_count_)] = frame_number;
    ++request_count_;

    return ds::Result<ds::RequestedVideoFrame>::success(
      ds::RequestedVideoFrame{
        input_index,
        frame_number,
        frames_[static_cast<std::size_t>(input_index)]
      }
    );
  }

  int request_count() const {
    return request_count_;
  }

  int requested_input(int index) const {
    return requested_inputs_[static_cast<std::size_t>(index)];
  }

  int requested_frame(int index) const {
    return requested_frames_[static_cast<std::size_t>(index)];
  }

private:
  std::array<ds::PlaneSpan<const unsigned char>, 3> frames_;
  std::array<int, 3> requested_inputs_{};
  std::array<int, 3> requested_frames_{};
  int request_count_ = 0;
};

} // namespace

TEST_CASE("AcceptanceTemporalAverage3 exposes descriptor metadata and initializes output info") {
  std::vector<ds::VideoInputInfo> inputs{
    ds::VideoInputInfo{640, 360, 12},
    ds::VideoInputInfo{640, 360, 12},
    ds::VideoInputInfo{640, 360, 12}
  };
  ds::VideoInitContext context{inputs};

  const auto result = ds::acceptance::AcceptanceTemporalAverage3::init(context);

  REQUIRE(std::string(ds::acceptance::AcceptanceTemporalAverage3::name) == "AcceptanceTemporalAverage3");
  REQUIRE(ds::acceptance::AcceptanceTemporalAverage3::input_count == 3);
  REQUIRE(ds::acceptance::AcceptanceTemporalAverage3::output_origin.kind == ds::OutputOriginKind::Fresh);
  REQUIRE(result.has_value());
  REQUIRE(result.value().output.width == 640);
  REQUIRE(result.value().output.height == 360);
  REQUIRE(result.value().output.num_frames == 12);
}

TEST_CASE("AcceptanceTemporalAverage3Bridge exposes host binding metadata") {
  using Bridge = ds::acceptance::AcceptanceTemporalAverage3Bridge;

  REQUIRE((std::is_same_v<Bridge::Core, ds::acceptance::AcceptanceTemporalAverage3>));
  REQUIRE(std::string(Bridge::vs_name) == "AcceptanceTemporalAverage3");
  REQUIRE(std::string(Bridge::vs_signature) == "a:vnode;b:vnode;c:vnode;");
  REQUIRE(Bridge::vs_input_names.size() == 3);
  REQUIRE(std::string(Bridge::vs_input_names[0]) == "a");
  REQUIRE(std::string(Bridge::vs_input_names[1]) == "b");
  REQUIRE(std::string(Bridge::vs_input_names[2]) == "c");
  REQUIRE(std::string(Bridge::avs_name) == "DSAcceptanceTemporalAverage3");
  REQUIRE(std::string(Bridge::avs_signature) == "ccc");
  REQUIRE(Bridge::parity_source_index == 1);
  REQUIRE(Bridge::forward_audio == false);
}

TEST_CASE("AcceptanceTemporalAverage3 rejects inputs with mismatched output info") {
  std::vector<ds::VideoInputInfo> inputs{
    ds::VideoInputInfo{640, 360, 12},
    ds::VideoInputInfo{640, 360, 12},
    ds::VideoInputInfo{320, 360, 12}
  };
  ds::VideoInitContext context{inputs};

  const auto result = ds::acceptance::AcceptanceTemporalAverage3::init(context);

  REQUIRE(!result.has_value());
  REQUIRE(result.error().code == ds::ErrorCode::InvalidArgument);
}

TEST_CASE("AcceptanceTemporalAverage3 declares temporal requests before processing") {
  std::vector<ds::VideoFrameRequest> requests;
  ds::VideoRequestContext context{5, requests};

  const auto result = ds::acceptance::AcceptanceTemporalAverage3::request(context);

  REQUIRE(result.has_value());
  REQUIRE(requests.size() == 3);
  REQUIRE(requests[0].input_index == 0);
  REQUIRE(requests[0].frame_number == 4);
  REQUIRE(requests[1].input_index == 1);
  REQUIRE(requests[1].frame_number == 5);
  REQUIRE(requests[2].input_index == 2);
  REQUIRE(requests[2].frame_number == 6);
}

TEST_CASE("AcceptanceTemporalAverage3 averages a[n-1], b[n], and c[n+1]") {
  const std::array<unsigned char, 4> a_storage{10, 20, 30, 40};
  const std::array<unsigned char, 4> b_storage{40, 50, 60, 70};
  const std::array<unsigned char, 4> c_storage{70, 80, 90, 100};
  std::array<unsigned char, 4> dst_storage{};

  FakeFrameProvider provider(
    ds::PlaneSpan<const unsigned char>(a_storage.data(), 2, 2, 2),
    ds::PlaneSpan<const unsigned char>(b_storage.data(), 2, 2, 2),
    ds::PlaneSpan<const unsigned char>(c_storage.data(), 2, 2, 2)
  );

  ds::VideoProcessContext context{
    5,
    provider,
    ds::PlaneSpan<unsigned char>(dst_storage.data(), 2, 2, 2)
  };

  const auto result = ds::acceptance::AcceptanceTemporalAverage3::process(context);

  REQUIRE(result.has_value());
  REQUIRE(dst_storage == std::array<unsigned char, 4>{40, 50, 60, 70});
  REQUIRE(provider.request_count() == 3);
  REQUIRE(provider.requested_input(0) == 0);
  REQUIRE(provider.requested_frame(0) == 4);
  REQUIRE(provider.requested_input(1) == 1);
  REQUIRE(provider.requested_frame(1) == 5);
  REQUIRE(provider.requested_input(2) == 2);
  REQUIRE(provider.requested_frame(2) == 6);
}
