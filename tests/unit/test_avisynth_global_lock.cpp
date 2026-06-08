#include <dualsynth/avisynth/global_lock.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <future>
#include <optional>

namespace {

using namespace std::chrono_literals;

} // namespace

TEST_CASE("AviSynth global lock guard falls back when no host environment is available") {
  ds::NamedGlobalLockRegistry fallback;
  std::optional<ds::avisynth::GlobalLockGuard> first;
  first.emplace(nullptr, "fftw", fallback);

  auto entered = std::promise<void>{};
  auto entered_future = entered.get_future();
  auto worker = std::async(
    std::launch::async,
    [&fallback, entered = std::move(entered)]() mutable {
      ds::avisynth::GlobalLockGuard second(nullptr, "fftw", fallback);
      entered.set_value();
    }
  );

  REQUIRE(entered_future.wait_for(50ms) == std::future_status::timeout);

  first.reset();

  REQUIRE(entered_future.wait_for(1s) == std::future_status::ready);
  worker.wait();
}
