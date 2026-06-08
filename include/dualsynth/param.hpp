#pragma once

#include <dualsynth/error.hpp>

#include <cstdint>
#include <string>
#include <utility>
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
  using Storage = std::variant<
    std::monostate,
    std::int64_t,
    double,
    bool,
    std::string,
    std::vector<std::int64_t>,
    std::vector<double>,
    std::vector<bool>,
    std::vector<std::string>
  >;

  Storage value{};

  ParamValue() = default;
  ParamValue(int input) : value(static_cast<std::int64_t>(input)) {}
  ParamValue(std::int64_t input) : value(input) {}
  ParamValue(float input) : value(static_cast<double>(input)) {}
  ParamValue(double input) : value(input) {}
  ParamValue(bool input) : value(input) {}
  ParamValue(const char* input) : value(std::string(input)) {}
  ParamValue(std::string input) : value(std::move(input)) {}
  ParamValue(std::vector<int> input);
  ParamValue(std::vector<std::int64_t> input) : value(std::move(input)) {}
  ParamValue(std::vector<float> input);
  ParamValue(std::vector<double> input) : value(std::move(input)) {}
  ParamValue(std::vector<bool> input) : value(std::move(input)) {}
  ParamValue(std::vector<std::string> input) : value(std::move(input)) {}
};

struct ParamSpec {
  std::string name;
  ParamType type;
  ParamValue default_value{};
  bool required = false;
  bool is_array = false;
  bool vs_enabled = true;
  bool avs_enabled = true;
};

struct ParamEntry {
  std::string name;
  ParamValue value;
};

struct ParamValues {
  std::vector<ParamEntry> entries;

  Result<int> get_int(const std::string& name, int default_value) const;
  Result<std::int64_t> get_int64(const std::string& name, std::int64_t default_value) const;
  Result<double> get_double(const std::string& name, double default_value) const;
  Result<bool> get_bool(const std::string& name, bool default_value) const;
  Result<std::string> get_string(const std::string& name, std::string default_value) const;
  Result<std::vector<std::int64_t>> get_int_array(
    const std::string& name,
    std::vector<std::int64_t> default_value
  ) const;
  Result<std::vector<double>> get_double_array(
    const std::string& name,
    std::vector<double> default_value
  ) const;
  Result<std::vector<bool>> get_bool_array(
    const std::string& name,
    std::vector<bool> default_value
  ) const;
  Result<std::vector<std::string>> get_string_array(
    const std::string& name,
    std::vector<std::string> default_value
  ) const;
};

struct FilterDescriptor {
  std::string name;
  std::vector<ParamSpec> params;
};

Result<bool> validate_param_spec(const ParamSpec& spec);
Result<bool> validate_filter_descriptor(const FilterDescriptor& descriptor);

} // namespace ds
