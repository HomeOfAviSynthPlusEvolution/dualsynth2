#pragma once

#include <avisynth.h>

#include <dualsynth/host_variable.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace ds::avisynth {

#if defined(__cpp_concepts) && __cpp_concepts >= 201907L
template <class Env>
concept HostVariableEnvironment = requires(
  Env* env,
  const char* name,
  const char* text,
  const AVSValue& value
) {
  { env->SaveString(text) } -> std::convertible_to<char*>;
  { env->SetVar(name, value) } -> std::convertible_to<bool>;
};
template <class Env>
inline constexpr bool is_host_variable_environment_v = HostVariableEnvironment<Env>;
#else
template <class Env, class = void>
struct is_host_variable_environment : std::false_type {};

template <class Env>
struct is_host_variable_environment<
  Env,
  std::void_t<
    decltype(std::declval<Env*>()->SaveString(std::declval<const char*>())),
    decltype(std::declval<Env*>()->SetVar(std::declval<const char*>(), std::declval<const AVSValue&>()))
  >
> : std::true_type {};

template <class Env>
inline constexpr bool is_host_variable_environment_v = is_host_variable_environment<Env>::value;
#endif

inline bool assign_avisynth_scalar_value(std::int64_t input, AVSValue& output) {
  if (
    input < static_cast<std::int64_t>(std::numeric_limits<int>::min()) ||
    input > static_cast<std::int64_t>(std::numeric_limits<int>::max())
  ) {
    return false;
  }

  output = AVSValue(static_cast<int>(input));
  return true;
}

inline bool assign_avisynth_scalar_value(double input, AVSValue& output) {
  output = AVSValue(input);
  return true;
}

inline bool assign_avisynth_scalar_value(bool input, AVSValue& output) {
  output = AVSValue(input);
  return true;
}

template <class Env>
bool assign_avisynth_scalar_value(Env*, std::int64_t input, AVSValue& output) {
  return assign_avisynth_scalar_value(input, output);
}

template <class Env>
bool assign_avisynth_scalar_value(Env*, double input, AVSValue& output) {
  return assign_avisynth_scalar_value(input, output);
}

template <class Env>
bool assign_avisynth_scalar_value(Env*, bool input, AVSValue& output) {
  return assign_avisynth_scalar_value(input, output);
}

template <class Env>
bool assign_avisynth_scalar_value(Env* env, const std::string& input, AVSValue& output) {
  if (!env) {
    return false;
  }

  output = AVSValue(env->SaveString(input.c_str()));
  return true;
}

template <class Env, class Value>
bool assign_avisynth_array_value(Env* env, const std::vector<Value>& input, AVSValue& output) {
  if (input.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return false;
  }

  std::vector<AVSValue> values;
  values.reserve(input.size());

  for (const auto& item : input) {
    AVSValue item_value;
    if (!assign_avisynth_scalar_value(env, item, item_value)) {
      return false;
    }
    values.push_back(item_value);
  }

  output = AVSValue(
    values.empty() ? nullptr : values.data(),
    static_cast<int>(values.size())
  );
  return true;
}

template <class Env>
bool assign_avisynth_value(Env* env, const ParamValue& input, AVSValue& output) {
  return std::visit(
    [&](const auto& value) -> bool {
      using Value = std::decay_t<decltype(value)>;

      if constexpr (std::is_same_v<Value, std::monostate>) {
        return false;
      } else if constexpr (
        std::is_same_v<Value, std::int64_t> ||
        std::is_same_v<Value, double> ||
        std::is_same_v<Value, bool> ||
        std::is_same_v<Value, std::string>
      ) {
        return assign_avisynth_scalar_value(env, value, output);
      } else {
        return assign_avisynth_array_value(env, value, output);
      }
    },
    input.value
  );
}

template <class Env, bool Supported = is_host_variable_environment_v<Env>>
struct HostVariableAdapter;

template <class Env>
struct HostVariableAdapter<Env, false> {
  static bool set(void*, const char*, const ParamValue&) {
    return true;
  }
};

template <class Env>
struct HostVariableAdapter<Env, true> {
  static bool set(void* user, const char* name, const ParamValue& value) {
    if (!user || !name) {
      return false;
    }

    auto* env = static_cast<Env*>(user);
    AVSValue avs_value;
    if (!assign_avisynth_value(env, value, avs_value)) {
      return false;
    }

    // AviSynth returns false when SetVar updates an existing variable.
    env->SetVar(name, avs_value);
    return true;
  }
};

inline HostVariableCallbacks host_variable_callbacks(IScriptEnvironment* env) noexcept {
  if (!env) {
    return {};
  }

  return HostVariableCallbacks{
    env,
    &HostVariableAdapter<IScriptEnvironment>::set
  };
}

} // namespace ds::avisynth
