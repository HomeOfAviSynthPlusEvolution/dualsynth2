#include <dualsynth/param.hpp>

#include <algorithm>
#include <cctype>
#include <limits>
#include <locale>
#include <sstream>
#include <string_view>

namespace ds {

namespace {

const ParamValue* find_param_value(const std::vector<ParamEntry>& entries, const std::string& name) {
  for (const auto& entry : entries) {
    if (entry.name == name) {
      return &entry.value;
    }
  }
  return nullptr;
}

Error type_error(const std::string& name, std::string_view expected) {
  return Error{
    ErrorCode::InvalidArgument,
    "parameter '" + name + "' must be " + std::string(expected)
  };
}

std::vector<std::string> split_array_tokens(std::string_view input) {
  std::string normalized(input);
  std::ranges::replace(normalized, ',', ' ');

  std::istringstream stream(normalized);
  stream.imbue(std::locale::classic());

  std::vector<std::string> tokens;
  std::string token;
  while (stream >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

template <class T>
bool parse_token(const std::string& token, T& output) {
  std::istringstream stream(token);
  stream.imbue(std::locale::classic());
  stream >> output;
  return !stream.fail() && stream.eof();
}

bool parse_token(const std::string& token, bool& output) {
  std::string lowered(token);
  std::ranges::transform(lowered, lowered.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });

  if (lowered == "true" || lowered == "1") {
    output = true;
    return true;
  }
  if (lowered == "false" || lowered == "0") {
    output = false;
    return true;
  }
  return false;
}

template <class T>
Result<std::vector<T>> parse_array_string(
  const std::string& name,
  const std::string& input,
  std::string_view expected
) {
  std::vector<T> output;
  for (const auto& token : split_array_tokens(input)) {
    T value{};
    if (!parse_token(token, value)) {
      return Result<std::vector<T>>::failure(type_error(name, expected));
    }
    output.push_back(std::move(value));
  }
  return Result<std::vector<T>>::success(std::move(output));
}

Result<std::vector<std::string>> parse_string_array_string(
  const std::string& input
) {
  return Result<std::vector<std::string>>::success(split_array_tokens(input));
}

template <class T>
Result<T> get_scalar(
  const std::vector<ParamEntry>& entries,
  const std::string& name,
  T default_value,
  std::string_view expected
) {
  const ParamValue* entry = find_param_value(entries, name);
  if (!entry) {
    return Result<T>::success(std::move(default_value));
  }

  if (const auto* value = std::get_if<T>(&entry->value)) {
    return Result<T>::success(*value);
  }

  return Result<T>::failure(type_error(name, expected));
}

template <class T>
Result<std::vector<T>> get_array(
  const std::vector<ParamEntry>& entries,
  const std::string& name,
  std::vector<T> default_value,
  std::string_view expected
) {
  const ParamValue* entry = find_param_value(entries, name);
  if (!entry) {
    return Result<std::vector<T>>::success(std::move(default_value));
  }

  if (const auto* value = std::get_if<std::vector<T>>(&entry->value)) {
    return Result<std::vector<T>>::success(*value);
  }

  if (const auto* value = std::get_if<std::string>(&entry->value)) {
    return parse_array_string<T>(name, *value, expected);
  }

  return Result<std::vector<T>>::failure(type_error(name, expected));
}

bool default_matches_spec(const ParamSpec& spec) {
  if (std::holds_alternative<std::monostate>(spec.default_value.value)) {
    return true;
  }

  switch (spec.type) {
  case ParamType::Clip:
    return false;
  case ParamType::Integer:
    return spec.is_array
      ? std::holds_alternative<std::vector<std::int64_t>>(spec.default_value.value)
      : std::holds_alternative<std::int64_t>(spec.default_value.value);
  case ParamType::Float:
    return spec.is_array
      ? std::holds_alternative<std::vector<double>>(spec.default_value.value)
      : std::holds_alternative<double>(spec.default_value.value);
  case ParamType::Boolean:
    return spec.is_array
      ? std::holds_alternative<std::vector<bool>>(spec.default_value.value)
      : std::holds_alternative<bool>(spec.default_value.value);
  case ParamType::String:
    return spec.is_array
      ? std::holds_alternative<std::vector<std::string>>(spec.default_value.value)
      : std::holds_alternative<std::string>(spec.default_value.value);
  }
  return false;
}

} // namespace

ParamValue::ParamValue(std::vector<int> input) {
  std::vector<std::int64_t> converted;
  converted.reserve(input.size());
  for (const int item : input) {
    converted.push_back(item);
  }
  value = std::move(converted);
}

ParamValue::ParamValue(std::vector<float> input) {
  std::vector<double> converted;
  converted.reserve(input.size());
  for (const float item : input) {
    converted.push_back(item);
  }
  value = std::move(converted);
}

Result<int> ParamValues::get_int(const std::string& name, int default_value) const {
  auto value = get_int64(name, default_value);
  if (!value.has_value()) {
    return Result<int>::failure(value.error());
  }
  if (
    value.value() < std::numeric_limits<int>::min() ||
    value.value() > std::numeric_limits<int>::max()
  ) {
    return Result<int>::failure(type_error(name, "an integer in int range"));
  }
  return Result<int>::success(static_cast<int>(value.value()));
}

Result<std::int64_t> ParamValues::get_int64(
  const std::string& name,
  std::int64_t default_value
) const {
  return get_scalar<std::int64_t>(entries, name, default_value, "an integer");
}

Result<double> ParamValues::get_double(
  const std::string& name,
  double default_value
) const {
  return get_scalar<double>(entries, name, default_value, "a float");
}

Result<bool> ParamValues::get_bool(
  const std::string& name,
  bool default_value
) const {
  return get_scalar<bool>(entries, name, default_value, "a boolean");
}

Result<std::string> ParamValues::get_string(
  const std::string& name,
  std::string default_value
) const {
  return get_scalar<std::string>(entries, name, std::move(default_value), "a string");
}

Result<std::vector<std::int64_t>> ParamValues::get_int_array(
  const std::string& name,
  std::vector<std::int64_t> default_value
) const {
  return get_array<std::int64_t>(
    entries,
    name,
    std::move(default_value),
    "an integer array"
  );
}

Result<std::vector<double>> ParamValues::get_double_array(
  const std::string& name,
  std::vector<double> default_value
) const {
  return get_array<double>(entries, name, std::move(default_value), "a float array");
}

Result<std::vector<bool>> ParamValues::get_bool_array(
  const std::string& name,
  std::vector<bool> default_value
) const {
  return get_array<bool>(entries, name, std::move(default_value), "a boolean array");
}

Result<std::vector<std::string>> ParamValues::get_string_array(
  const std::string& name,
  std::vector<std::string> default_value
) const {
  const ParamValue* entry = find_param_value(entries, name);
  if (!entry) {
    return Result<std::vector<std::string>>::success(std::move(default_value));
  }

  if (const auto* value = std::get_if<std::vector<std::string>>(&entry->value)) {
    return Result<std::vector<std::string>>::success(*value);
  }

  if (const auto* value = std::get_if<std::string>(&entry->value)) {
    return parse_string_array_string(*value);
  }

  return Result<std::vector<std::string>>::failure(type_error(name, "a string array"));
}

Result<bool> validate_param_spec(const ParamSpec& spec) {
  if (spec.name.empty()) {
    return Result<bool>::failure({
      ErrorCode::InvalidArgument,
      "parameter name must not be empty"
    });
  }

  if (!spec.vs_enabled && !spec.avs_enabled) {
    return Result<bool>::failure({
      ErrorCode::InvalidArgument,
      "parameter '" + spec.name + "' must be enabled for at least one host"
    });
  }

  if (!default_matches_spec(spec)) {
    return Result<bool>::failure({
      ErrorCode::InvalidArgument,
      "parameter '" + spec.name + "' default value does not match its schema"
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
