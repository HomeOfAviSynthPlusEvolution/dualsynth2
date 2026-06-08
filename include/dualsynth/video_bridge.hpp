#pragma once

#include <dualsynth/error.hpp>
#include <dualsynth/param.hpp>

#include <array>
#include <cstddef>
#include <string>

namespace ds {

template <class CoreFilter>
struct SingleInputVideoBridgeDefaults {
  using Core = CoreFilter;

  static constexpr const char* vs_signature = "clip:vnode;";
  static constexpr std::array<const char*, static_cast<std::size_t>(Core::input_count)> vs_input_names{
    "clip"
  };

  static constexpr const char* avs_signature = "c";
  static constexpr const char* missing_input_error = "DualSynth: missing required video clip";
  static constexpr std::size_t parity_source_index = 0;
  static constexpr bool forward_audio = true;
};

template <class Bridge>
concept VideoBridge = requires {
  typename Bridge::Core;
  Bridge::vs_name;
  Bridge::vs_signature;
  Bridge::vs_input_names;
  Bridge::avs_name;
  Bridge::avs_signature;
  Bridge::missing_input_error;
  Bridge::vs_format_error;
  Bridge::avs_format_error;
  Bridge::parity_source_index;
  Bridge::forward_audio;
};

Result<std::string> make_vapoursynth_signature(const FilterDescriptor& descriptor);
Result<std::string> make_avisynth_signature(const FilterDescriptor& descriptor);

} // namespace ds
