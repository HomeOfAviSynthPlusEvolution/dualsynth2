#pragma once

#include <dualsynth/error.hpp>
#include <dualsynth/param.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace ds {

struct HostVariableCallbacks {
  void* user = nullptr;
  bool (*set)(void* user, const char* name, const ParamValue& value) = nullptr;
};

inline Result<bool> set_host_variable(
  HostVariableCallbacks callbacks,
  std::string_view name,
  ParamValue value
) {
  if (name.empty()) {
    return Result<bool>::failure(
      Error{ErrorCode::InvalidArgument, "DualSynth: host variable name must not be empty"}
    );
  }

  if (!callbacks.set) {
    return Result<bool>::success(true);
  }

  const std::string owned_name{name};
  if (callbacks.set(callbacks.user, owned_name.c_str(), value)) {
    return Result<bool>::success(true);
  }

  return Result<bool>::failure(
    Error{ErrorCode::HostError, "DualSynth: host rejected script variable assignment"}
  );
}

} // namespace ds
