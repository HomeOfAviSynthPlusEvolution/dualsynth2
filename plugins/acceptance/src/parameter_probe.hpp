#pragma once

#include <dualsynth/video_bridge.hpp>
#include <limits>
#include <stdexcept>

namespace ds::acceptance {

// Synthetic contract probe: verifies bridge values before any downstream policy.
struct ParameterProbe {
  static constexpr int input_count = dynamic_video_inputs;
  static constexpr const char* name = "ParameterProbe";
  static constexpr OutputOrigin output_origin = OutputOrigin::copy_from_input(0);
  static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
  }
  static Result<VideoInitResult> init(VideoInitContext& ctx) {
    const auto& p = *ctx.params;
    const auto mode = p.get_string("mode", "full").value();
    if (mode == "full") {
      check(p.get_int64("integer", 0).value() == INT64_MAX, "integer precision");
      check(p.get_double("number", 0).value() == 1.0000000000000002, "double precision");
      check(p.get_int_array("ints", {}).value() == std::vector<std::int64_t>{INT64_MIN, INT64_MAX}, "integer array precision");
      check(p.get_double_array("floats", {}).value() == std::vector<double>{1.0000000000000002, -2.5}, "double array precision");
      check(p.get_int_array("legacy", {}).value() == std::vector<std::int64_t>{3,4}, "legacy slot mapping");
      check(ctx.inputs.size() == 3 && ctx.inputs[1].width == 8 && ctx.inputs[2].width == 12, "clip array order");
    } else if (mode == "empty") {
      check(p.get_int_array("ints", {1}).value().empty(), "empty integer array");
      check(p.get_double_array("floats", {1}).value().empty(), "empty float array");
      check(p.get_int64("integer", 1).value() == 0, "explicit zero");
      check(!p.get_bool("flag", true).value(), "explicit false");
      check(ctx.inputs.size() == 1, "empty clip group");
      check(ctx.input_groups[2].provided && ctx.input_groups[2].count == 0, "provided empty clip array");
    } else if (mode == "missing") {
      check(p.get_int_array("ints", {1}).value() == std::vector<std::int64_t>{1}, "missing integer array");
      check(p.get_double_array("floats", {1}).value() == std::vector<double>{1}, "missing float array");
      check(p.get_int64("integer", 17).value() == 17, "missing integer");
      check(p.get_bool("flag", true).value(), "missing boolean");
      check(!ctx.input_groups[2].provided && ctx.input_groups[2].count == 0, "missing clip array");
    } else {
      throw std::runtime_error("unknown probe mode");
    }
    const auto& in = ctx.inputs[0];
    return Result<VideoInitResult>::success({{in.width,in.height,in.num_frames,in.format,in.fps}});
  }
  static Result<VideoProcessResult> process(VideoProcessContext&) {
    return Result<VideoProcessResult>::success({});
  }
};

struct ParameterProbeBridge {
  using Core = ParameterProbe;
  static constexpr const char* avs_name = "DSParameterProbe";
  static constexpr const char* avs_signature = "";
  static constexpr const char* missing_input_error = "ParameterProbe requires a clip";
  static constexpr const char* avs_format_error = "ParameterProbe requires planar video";
  static constexpr std::size_t parity_source_index = 0;
  static constexpr bool forward_audio = false;
  static FilterDescriptor descriptor() {
    return {"ParameterProbe", {
      {"clip",ParamType::Clip,{},true},
      {"ints",ParamType::Integer,{},false,true,true,true,AvisynthArrayBinding::Native},
      {"legacy",ParamType::Integer,{},false,true},
      {"floats",ParamType::Float,{},false,true,true,true,AvisynthArrayBinding::Native},
      {"clips",ParamType::Clip,{},true,true,true,true,AvisynthArrayBinding::Native},
      {"integer",ParamType::Integer},
      {"number",ParamType::Float},
      {"flag",ParamType::Boolean},
      {"mode",ParamType::String},
      {"extra",ParamType::Clip,{},false,true,true,true,AvisynthArrayBinding::Native}
    }};
  }
};
} // namespace ds::acceptance
