#include <dualsynth/param.hpp>

namespace ds {

Result<bool> validate_param_spec(const ParamSpec& spec) {
  if (spec.name.empty()) {
    return Result<bool>::failure({
      ErrorCode::InvalidArgument,
      "parameter name must not be empty"
    });
  }

  return Result<bool>::success(true);
}

Result<bool> validate_filter_descriptor(const FilterDescriptor& descriptor) {
  if (descriptor.name.empty()) {
    return Result<bool>::failure({
      ErrorCode::InvalidArgument,
      "filter name must not be empty"
    });
  }

  for (const auto& param : descriptor.params) {
    auto result = validate_param_spec(param);
    if (!result.has_value()) {
      return result;
    }
  }

  return Result<bool>::success(true);
}

} // namespace ds
