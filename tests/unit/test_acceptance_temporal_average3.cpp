#include <array>
#include <catch2/catch_test_macros.hpp>
#include <dualsynth/acceptance/temporal_average3.hpp>

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

  const auto result = ds::acceptance::temporal_average3_process(context);

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
