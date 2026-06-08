#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ds {

class NamedGlobalLockRegistry {
public:
  std::shared_ptr<std::mutex> mutex_for(std::string_view name) {
    auto key = std::string{name};

    std::lock_guard guard(registry_mutex_);
    auto& mutex = locks_[std::move(key)];
    if (!mutex) {
      mutex = std::make_shared<std::mutex>();
    }
    return mutex;
  }

private:
  std::mutex registry_mutex_;
  std::unordered_map<std::string, std::shared_ptr<std::mutex>> locks_;
};

inline NamedGlobalLockRegistry& process_global_lock_registry() {
  static NamedGlobalLockRegistry registry;
  return registry;
}

struct HostContext {
  NamedGlobalLockRegistry* global_locks = &process_global_lock_registry();

  NamedGlobalLockRegistry& global_lock_registry() const {
    if (global_locks) {
      return *global_locks;
    }
    return process_global_lock_registry();
  }
};

inline HostContext default_host_context() {
  return HostContext{&process_global_lock_registry()};
}

class GlobalLockGuard {
public:
  GlobalLockGuard() = default;

  GlobalLockGuard(NamedGlobalLockRegistry& registry, std::string_view name) {
    lock(registry, name);
  }

  GlobalLockGuard(HostContext context, std::string_view name)
    : GlobalLockGuard(context.global_lock_registry(), name) {}

  explicit GlobalLockGuard(std::string_view name)
    : GlobalLockGuard(process_global_lock_registry(), name) {}

  GlobalLockGuard(const GlobalLockGuard&) = delete;
  GlobalLockGuard& operator=(const GlobalLockGuard&) = delete;
  GlobalLockGuard(GlobalLockGuard&& other) noexcept
    : mutex_(std::move(other.mutex_)),
      lock_(std::move(other.lock_)) {}

  GlobalLockGuard& operator=(GlobalLockGuard&& other) noexcept {
    if (this != &other) {
      lock_ = std::unique_lock<std::mutex>{};
      mutex_.reset();

      mutex_ = std::move(other.mutex_);
      lock_ = std::move(other.lock_);
    }
    return *this;
  }
  ~GlobalLockGuard() = default;

  bool owns_lock() const noexcept {
    return lock_.owns_lock();
  }

private:
  void lock(NamedGlobalLockRegistry& registry, std::string_view name) {
    if (name.empty()) {
      return;
    }

    mutex_ = registry.mutex_for(name);
    lock_ = std::unique_lock<std::mutex>{*mutex_};
  }

  std::shared_ptr<std::mutex> mutex_;
  std::unique_lock<std::mutex> lock_;
};

struct HostGlobalLockCallbacks {
  void* user = nullptr;
  bool (*acquire)(void* user, const char* name) = nullptr;
  void (*release)(void* user, const char* name) = nullptr;
};

class HostGlobalLockGuard {
public:
  HostGlobalLockGuard() = default;

  HostGlobalLockGuard(
    std::string_view name,
    HostGlobalLockCallbacks callbacks,
    NamedGlobalLockRegistry& fallback_registry = process_global_lock_registry()
  ) : name_(name),
      callbacks_(callbacks) {
    acquire(fallback_registry);
  }

  HostGlobalLockGuard(
    std::string_view name,
    HostGlobalLockCallbacks callbacks,
    HostContext context
  ) : HostGlobalLockGuard(name, callbacks, context.global_lock_registry()) {}

  HostGlobalLockGuard(const HostGlobalLockGuard&) = delete;
  HostGlobalLockGuard& operator=(const HostGlobalLockGuard&) = delete;

  HostGlobalLockGuard(HostGlobalLockGuard&& other) noexcept
    : name_(std::move(other.name_)),
      callbacks_(other.callbacks_),
      host_acquired_(std::exchange(other.host_acquired_, false)),
      fallback_lock_(std::move(other.fallback_lock_)) {}

  HostGlobalLockGuard& operator=(HostGlobalLockGuard&& other) noexcept {
    if (this != &other) {
      release_host_lock();
      fallback_lock_.reset();

      name_ = std::move(other.name_);
      callbacks_ = other.callbacks_;
      host_acquired_ = std::exchange(other.host_acquired_, false);
      fallback_lock_ = std::move(other.fallback_lock_);
    }
    return *this;
  }

  ~HostGlobalLockGuard() {
    release_host_lock();
  }

  bool owns_host_lock() const noexcept {
    return host_acquired_;
  }

  bool owns_fallback_lock() const noexcept {
    return fallback_lock_.has_value() && fallback_lock_->owns_lock();
  }

private:
  void acquire(NamedGlobalLockRegistry& fallback_registry) {
    if (name_.empty()) {
      return;
    }

    if (
      callbacks_.acquire &&
      callbacks_.release &&
      callbacks_.acquire(callbacks_.user, name_.c_str())
    ) {
      host_acquired_ = true;
      return;
    }

    fallback_lock_.emplace(fallback_registry, name_);
  }

  void release_host_lock() noexcept {
    if (!host_acquired_) {
      return;
    }

    callbacks_.release(callbacks_.user, name_.c_str());
    host_acquired_ = false;
  }

  std::string name_;
  HostGlobalLockCallbacks callbacks_;
  bool host_acquired_ = false;
  std::optional<GlobalLockGuard> fallback_lock_;
};

} // namespace ds
