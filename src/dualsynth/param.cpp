#include <dualsynth/param.hpp>

namespace ds {

Result<int> ParamValues::get_int(const std::string& name, int default_value) const {
  for (const auto& entry : entries) {
    if (entry.name == name) {
      if (const auto* value = std::get_if<int>(&entry.value.value)) {
        return Result<int>::success(*value);
      }
      return Result<int>::failure({
        ErrorCode::InvalidArgument,
        "parameter '" + name + "' must be an integer"
      });
    }
  }
  return Result<int>::success(default_value);
}

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
