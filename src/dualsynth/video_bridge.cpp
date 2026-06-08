#include <dualsynth/video_bridge.hpp>

#include <sstream>
#include <string_view>

namespace ds {

namespace {

std::string_view vapoursynth_type_name(ParamType type) {
  switch (type) {
  case ParamType::Clip:
    return "vnode";
  case ParamType::Integer:
    return "int";
  case ParamType::Float:
    return "float";
  case ParamType::Boolean:
    return "int";
  case ParamType::String:
    return "data";
  }
  return "";
}

char avisynth_type_name(ParamType type) {
  switch (type) {
  case ParamType::Clip:
    return 'c';
  case ParamType::Integer:
    return 'i';
  case ParamType::Float:
    return 'f';
  case ParamType::Boolean:
    return 'b';
  case ParamType::String:
    return 's';
  }
  return '\0';
}

} // namespace

Result<std::string> make_vapoursynth_signature(const FilterDescriptor& descriptor) {
  auto validation = validate_filter_descriptor(descriptor);
  if (!validation.has_value()) {
    return Result<std::string>::failure(validation.error());
  }

  std::ostringstream signature;
  for (const auto& param : descriptor.params) {
    if (!param.vs_enabled) {
      continue;
    }

    signature << param.name << ':' << vapoursynth_type_name(param.type);
    if (param.is_array) {
      signature << "[]";
    }
    if (!param.required) {
      signature << ":opt";
    }
    signature << ';';
  }

  return Result<std::string>::success(signature.str());
}

Result<std::string> make_avisynth_signature(const FilterDescriptor& descriptor) {
  auto validation = validate_filter_descriptor(descriptor);
  if (!validation.has_value()) {
    return Result<std::string>::failure(validation.error());
  }

  std::ostringstream signature;
  std::ostringstream array_overloads;

  for (const auto& param : descriptor.params) {
    if (!param.avs_enabled) {
      continue;
    }

    const char type_name = avisynth_type_name(param.type);
    if (param.required) {
      if (param.is_array) {
        return Result<std::string>::failure({
          ErrorCode::InvalidArgument,
          "required AviSynth array parameter '" + param.name + "' is not supported"
        });
      }
      signature << type_name;
      continue;
    }

    signature << '[' << param.name << ']';
    if (param.is_array) {
      signature << 's';
      array_overloads << '[' << param.name << "()]";
      array_overloads << type_name;
    } else {
      signature << type_name;
    }
  }

  return Result<std::string>::success(signature.str() + array_overloads.str());
}

} // namespace ds
