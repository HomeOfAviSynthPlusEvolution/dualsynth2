#include <dualsynth/global_lock.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <future>
#include <optional>
#include <string>

namespace {

using namespace std::chrono_literals;

struct FakeHostLock {
  bool acquire_result = true;
  int acquire_calls = 0;
  int release_calls = 0;
  std::string acquired_name;
  std::string released_name;

  static bool acquire(void* user, const char* name) {
    auto& self = *static_cast<FakeHostLock*>(user);
    ++self.acquire_calls;
    self.acquired_name = name;
    return self.acquire_result;
  }

  static void release(void* user, const char* name) {
    auto& self = *static_cast<FakeHostLock*>(user);
    ++self.release_calls;
    self.released_name = name;
  }
};

} // namespace

TEST_CASE("Named global locks serialize holders of the same name") {
  ds::NamedGlobalLockRegistry registry;
  std::optional<ds::GlobalLockGuard> first;
  first.emplace(registry, "fftw");

  auto entered = std::promise<void>{};
  auto entered_future = entered.get_future();
  auto worker = std::async(std::launch::async, [&registry, entered = std::move(entered)]() mutable {
    ds::GlobalLockGuard second(registry, "fftw");
    entered.set_value();
  });

  REQUIRE(entered_future.wait_for(50ms) == std::future_status::timeout);

  first.reset();

  REQUIRE(entered_future.wait_for(1s) == std::future_status::ready);
  worker.wait();
}

TEST_CASE("Named global locks do not serialize unrelated names") {
  ds::NamedGlobalLockRegistry registry;
  ds::GlobalLockGuard first(registry, "fftw");

  auto entered = std::promise<void>{};
  auto entered_future = entered.get_future();
  auto worker = std::async(std::launch::async, [&registry, entered = std::move(entered)]() mutable {
    ds::GlobalLockGuard second(registry, "not-fftw");
    entered.set_value();
  });

  REQUIRE(entered_future.wait_for(1s) == std::future_status::ready);
  worker.wait();
}

TEST_CASE("Host global lock guard prefers a successful host lock") {
  ds::NamedGlobalLockRegistry fallback;
  FakeHostLock host;
  const ds::HostGlobalLockCallbacks callbacks{
    &host,
    &FakeHostLock::acquire,
    &FakeHostLock::release
  };

  {
    ds::HostGlobalLockGuard guard("fftw", callbacks, fallback);

    REQUIRE(host.acquire_calls == 1);
    REQUIRE(host.acquired_name == "fftw");
  }

  REQUIRE(host.release_calls == 1);
  REQUIRE(host.released_name == "fftw");
}

TEST_CASE("Host global lock guard falls back when the host cannot acquire") {
  ds::NamedGlobalLockRegistry fallback;
  FakeHostLock host;
  host.acquire_result = false;
  const ds::HostGlobalLockCallbacks callbacks{
    &host,
    &FakeHostLock::acquire,
    &FakeHostLock::release
  };

  std::optional<ds::HostGlobalLockGuard> first;
  first.emplace("fftw", callbacks, fallback);

  auto entered = std::promise<void>{};
  auto entered_future = entered.get_future();
  auto worker = std::async(
    std::launch::async,
    [&fallback, entered = std::move(entered)]() mutable {
      ds::HostGlobalLockGuard second("fftw", {}, fallback);
      entered.set_value();
    }
  );

  REQUIRE(host.acquire_calls == 1);
  REQUIRE(host.release_calls == 0);
  REQUIRE(entered_future.wait_for(50ms) == std::future_status::timeout);

  first.reset();

  REQUIRE(entered_future.wait_for(1s) == std::future_status::ready);
  worker.wait();
}
