#pragma once

#include <avisynth.h>

#include <dualsynth/global_lock.hpp>

#include <concepts>
#include <string_view>

namespace ds::avisynth {

#if defined(__cpp_concepts) && __cpp_concepts >= 201907L
template <class Env>
concept HostGlobalLockEnvironment = requires(Env* env, const char* name) {
  { env->AcquireGlobalLock(name) } -> std::convertible_to<bool>;
  env->ReleaseGlobalLock(name);
};
template <class Env>
inline constexpr bool is_host_global_lock_environment_v = HostGlobalLockEnvironment<Env>;
#else
template <class Env, class = void>
struct is_host_global_lock_environment : std::false_type {};

template <class Env>
struct is_host_global_lock_environment<
  Env,
  std::void_t<
    decltype(std::declval<Env*>()->AcquireGlobalLock(std::declval<const char*>())),
    decltype(std::declval<Env*>()->ReleaseGlobalLock(std::declval<const char*>()))
  >
> : std::true_type {};

template <class Env>
inline constexpr bool is_host_global_lock_environment_v = is_host_global_lock_environment<Env>::value;
#endif

inline constexpr bool compiled_with_host_global_locks =
  is_host_global_lock_environment_v<IScriptEnvironment>;

inline bool runtime_supports_host_global_locks(IScriptEnvironment* env) noexcept {
  if (!env) {
    return false;
  }

  if constexpr (compiled_with_host_global_locks) {
    try {
      env->CheckVersion(12);
      return true;
    } catch (...) {
      return false;
    }
  } else {
    return false;
  }
}

template <class Env, bool Supported = is_host_global_lock_environment_v<Env>>
struct HostGlobalLockAdapter;

template <class Env>
struct HostGlobalLockAdapter<Env, false> {
  static bool acquire(void*, const char*) {
    return false;
  }

  static void release(void*, const char*) {}
};

template <class Env>
struct HostGlobalLockAdapter<Env, true> {
  static bool acquire(void* user, const char* name) {
    return static_cast<Env*>(user)->AcquireGlobalLock(name);
  }

  static void release(void* user, const char* name) {
    static_cast<Env*>(user)->ReleaseGlobalLock(name);
  }
};

inline HostGlobalLockCallbacks host_global_lock_callbacks(IScriptEnvironment* env) noexcept {
  if constexpr (compiled_with_host_global_locks) {
    if (runtime_supports_host_global_locks(env)) {
      return HostGlobalLockCallbacks{
        env,
        &HostGlobalLockAdapter<IScriptEnvironment>::acquire,
        &HostGlobalLockAdapter<IScriptEnvironment>::release
      };
    }
  }

  return {};
}

class GlobalLockGuard : public ds::HostGlobalLockGuard {
public:
  GlobalLockGuard(
    IScriptEnvironment* env,
    std::string_view name,
    NamedGlobalLockRegistry& fallback_registry = process_global_lock_registry()
  ) : ds::HostGlobalLockGuard(name, host_global_lock_callbacks(env), fallback_registry) {}

  GlobalLockGuard(
    IScriptEnvironment* env,
    std::string_view name,
    HostContext context
  ) : GlobalLockGuard(env, name, context.global_lock_registry()) {}
};

} // namespace ds::avisynth
