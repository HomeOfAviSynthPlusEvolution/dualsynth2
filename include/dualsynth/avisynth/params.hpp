#pragma once

#include <dualsynth/param.hpp>
#include <stdexcept>

namespace ds::avisynth::param_detail {

template <class Source>
Result<ParamValues> read_params_from_source(
  const Source& source,
  const FilterDescriptor& descriptor
) {
  auto validation = validate_filter_descriptor(descriptor);
  if (!validation.has_value()) {
    return Result<ParamValues>::failure(validation.error());
  }

  int base_count = 0;
  for (const auto& param : descriptor.params) {
    if (param.avs_enabled) {
      ++base_count;
    }
  }

  ParamValues values{};
  int base_index = 0;
  int array_index = base_count;

  for (const auto& param : descriptor.params) {
    if (!param.avs_enabled) {
      continue;
    }

    const int current_base_index = base_index++;
    if (param.type == ParamType::Clip) {
      continue;
    }

    try {
      if (param.is_array) {
        const bool native = avisynth_native_array(param);
        const int current_array_index = native ? current_base_index : array_index++;
        if (source.defined(current_array_index)) {
          switch (param.type) {
          case ParamType::Integer:
            values.entries.push_back(ParamEntry{
              param.name,
              ParamValue{source.as_int_array(current_array_index)}
            });
            break;
          case ParamType::Float:
            values.entries.push_back(ParamEntry{
              param.name,
              ParamValue{source.as_float_array(current_array_index)}
            });
            break;
          case ParamType::Boolean:
            values.entries.push_back(ParamEntry{
              param.name,
              ParamValue{source.as_bool_array(current_array_index)}
            });
            break;
          case ParamType::String:
            values.entries.push_back(ParamEntry{
              param.name,
              ParamValue{source.as_string_array(current_array_index)}
            });
            break;
          case ParamType::Clip:
            break;
          }
          continue;
        }

        if (!native && source.defined(current_base_index)) {
          values.entries.push_back(ParamEntry{
            param.name,
            ParamValue{source.as_string(current_base_index)}
          });
          continue;
        }

        if (param.required) {
          return Result<ParamValues>::failure({
            ErrorCode::InvalidArgument,
            "missing required AviSynth array parameter '" + param.name + "'"
          });
        }
        continue;
      }

      if (!source.defined(current_base_index)) {
        if (param.required) {
          return Result<ParamValues>::failure({
            ErrorCode::InvalidArgument,
            "missing required AviSynth parameter '" + param.name + "'"
          });
        }
        continue;
      }

      switch (param.type) {
      case ParamType::Integer:
        values.entries.push_back(ParamEntry{
          param.name,
          ParamValue{source.as_int(current_base_index)}
        });
        break;
      case ParamType::Float:
        values.entries.push_back(ParamEntry{
          param.name,
          ParamValue{source.as_float(current_base_index)}
        });
        break;
      case ParamType::Boolean:
        values.entries.push_back(ParamEntry{
          param.name,
          ParamValue{source.as_bool(current_base_index)}
        });
        break;
      case ParamType::String:
        values.entries.push_back(ParamEntry{
          param.name,
          ParamValue{source.as_string(current_base_index)}
        });
        break;
      case ParamType::Clip:
        break;
      }
    } catch (const std::exception& error) {
      return Result<ParamValues>::failure({
        ErrorCode::InvalidArgument,
        "AviSynth parameter '" + param.name + "': " + error.what()
      });
    } catch (...) {
      return Result<ParamValues>::failure({
        ErrorCode::InvalidArgument,
        "AviSynth parameter '" + param.name + "' has the wrong type"
      });
    }
  }

  return Result<ParamValues>::success(std::move(values));
}

} // namespace ds::avisynth::param_detail
