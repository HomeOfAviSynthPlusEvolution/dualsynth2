#pragma once

#include <dualsynth/error.hpp>

#include <string>
#include <variant>
#include <vector>

namespace ds {

enum class ParamType {
  Clip,
  Integer,
  Float,
  Boolean,
  String,
};

struct ParamValue {
  std::variant<int, double, bool, std::string> value;
};

struct ParamSpec {
  std::string name;
  ParamType type;
  ParamValue default_value;
  bool required;
};

struct FilterDescriptor {
  std::string name;
  std::vector<ParamSpec> params;
};

Result<bool> validate_param_spec(const ParamSpec& spec);
Result<bool> validate_filter_descriptor(const FilterDescriptor& descriptor);

} // namespace ds
