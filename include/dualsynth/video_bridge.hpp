#pragma once

#include <dualsynth/error.hpp>
#include <dualsynth/param.hpp>
#include <dualsynth/video_filter.hpp>

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

#if defined(__cpp_concepts) && __cpp_concepts >= 201907L
template <class Bridge>
concept VideoBridge = requires {
  typename Bridge::Core;
  Bridge::vs_name;
  Bridge::vs_signature;
  requires (Bridge::Core::input_count == dynamic_video_inputs && requires { Bridge::descriptor(); }) ||
           requires { Bridge::vs_input_names; };
  Bridge::avs_name;
  Bridge::avs_signature;
  Bridge::missing_input_error;
  Bridge::vs_format_error;
  Bridge::avs_format_error;
  Bridge::parity_source_index;
  Bridge::forward_audio;
};
#define DS_CONCEPT_VIDEO_BRIDGE ::ds::VideoBridge
#else
#define DS_CONCEPT_VIDEO_BRIDGE class
#endif

struct ClipInputSpec {
  std::string name;
  std::size_t argument;
  bool optional = false;
  bool array = false;
};

template<class Bridge>
std::vector<ClipInputSpec> bridge_clip_inputs(bool vapoursynth) {
  std::vector<ClipInputSpec> result;
  if constexpr (Bridge::Core::input_count == dynamic_video_inputs) {
    auto descriptor = Bridge::descriptor();
    std::size_t argument = 0;
    for (const auto& param : descriptor.params) {
      if (!(vapoursynth ? param.vs_enabled : param.avs_enabled)) continue;
      if (param.type == ParamType::Clip)
        result.push_back({param.name, argument, !param.required, param.is_array});
      ++argument;
    }
  } else {
    for (int i = 0; i < Bridge::Core::input_count; ++i)
      result.push_back({Bridge::vs_input_names[i], static_cast<std::size_t>(i), false, false});
  }
  return result;
}

Result<std::string> make_vapoursynth_signature(const FilterDescriptor& descriptor);
Result<std::string> make_avisynth_signature(const FilterDescriptor& descriptor);

} // namespace ds
