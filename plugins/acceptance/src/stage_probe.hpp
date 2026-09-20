#pragma once
#include <dualsynth/staged_video.hpp>
#include <dualsynth/video_bridge.hpp>

namespace ds::acceptance {
struct StageProbe {
  static constexpr const char* name = "StageProbe";
  static constexpr int input_count = dynamic_video_inputs;
  static constexpr HostRequirements host_requirements{true,11,0};
  static constexpr OutputOrigin output_origin = OutputOrigin::fresh_without_props();
  struct State { int mode; };
  struct RequestState { int phase = 0; int n = -1; int selected = -1; FrameRef current; };
  static Result<VideoInitStateResult<State>> init(VideoInitContext& ctx) {
    if (ctx.inputs.size() != 4) throw std::invalid_argument("StageProbe requires four clips");
    const auto& in = ctx.inputs[0];
    for (const auto& i : ctx.inputs)
      if (i.width != in.width || i.height != in.height || i.format != in.format || i.num_frames != in.num_frames)
        throw std::invalid_argument("StageProbe requires matching clips");
    return Result<VideoInitStateResult<State>>::success({
      {in.width,in.height,in.num_frames,in.format,in.fps}, {ctx.params->get_int("mode",0).value()}});
  }
  static Result<VideoStageResult> advance(VideoStageContext& ctx, RequestState& request) {
    const int mode = ctx.state<State>().mode;
    if (mode >= 6) {
      if (mode == 8 || (mode == 6 && ctx.output_frame % 2 != 0) ||
          (mode == 7 && ctx.output_frame % 2 != 1))
        throw std::runtime_error("upstream selected reference detail");
      ctx.origin = OutputOrigin::copy_from_input(0);
      return Result<VideoStageResult>::success(VideoStageResult::Ready);
    }
    if (mode == 5) return Result<VideoStageResult>::success(VideoStageResult::RequestFrames);
    if (request.phase++ == 0) {
      request.n = ctx.output_frame;
      ctx.request_frame(0,request.n);
      ctx.request_frame(1,request.n);
      ctx.request_frame(0,request.n); // Duplicates must not fetch twice.
      return Result<VideoStageResult>::success(VideoStageResult::RequestFrames);
    }
    if (request.phase == 2) {
      request.current = ctx.frames.get(0,request.n).value().owner;
      const auto vectors = ctx.frames.get(1,request.n).value();
      const auto* props = vectors.frame.properties;
      const auto info = props->inspect("Select");
      if (!info || info->type != PropertyType::Integer || info->count != 1)
        throw std::runtime_error("StageProbe: invalid Select metadata");
      const auto value = props->find("Select");
      request.selected = static_cast<int>(std::get<std::vector<std::int64_t>>(*value)[0]);
      if (request.selected < 0 || request.selected > 1) throw std::runtime_error("StageProbe: invalid selection");
      request.selected += 2;
      if (mode == 2) throw std::runtime_error("stage exception: original detail");
      if (mode == 3) return Result<VideoStageResult>::failure({ErrorCode::HostError,"stage result: original detail"});
      if (mode == 1) {
        ctx.origin = OutputOrigin::copy_from_input(0);
        return Result<VideoStageResult>::success(VideoStageResult::Ready);
      }
      ctx.request_frame(request.selected,request.n);
      return Result<VideoStageResult>::success(VideoStageResult::RequestFrames);
    }
    if (!ctx.frames.get(request.selected,request.n).has_value()) throw std::runtime_error("missing selected frame");
    ctx.origin = OutputOrigin::copy_from_input(request.selected,0);
    return Result<VideoStageResult>::success(VideoStageResult::Ready);
  }
  static Result<VideoProcessResult> process(VideoProcessContext& ctx, RequestState& request) {
    if (ctx.state<State>().mode >= 6) return Result<VideoProcessResult>::success({});
    if (request.n != ctx.output_frame || !request.current) throw std::runtime_error("shared request state");
    const auto retained = request.current.view().properties->find("Stamp");
    if (!retained || std::get<std::vector<std::int64_t>>(*retained)[0] != ctx.output_frame)
      throw std::runtime_error("lost earlier-stage frame");
    if (ctx.state<State>().mode == 4) throw std::runtime_error("process exception: original detail");
    ctx.dst.properties->set("Stage",std::vector<std::int64_t>{request.n});
    return Result<VideoProcessResult>::success({});
  }
};
struct StageProbeBridge {
  using Core = StageProbe;
  static constexpr const char* vs_name = "StageProbe";
  static constexpr const char* avs_name = "DSStageProbe";
  static constexpr const char* vs_signature = "clips:vnode[];mode:int:opt;";
  static constexpr const char* avs_signature = ".[mode]i";
  static constexpr const char* missing_input_error = "StageProbe requires clips";
  static constexpr const char* avs_format_error = "StageProbe requires planar video";
  static constexpr const char* vs_format_error = avs_format_error;
  static constexpr std::size_t parity_source_index = 0;
  static constexpr bool forward_audio = false;
  static FilterDescriptor descriptor() {
    return {"StageProbe",{{"clips",ParamType::Clip,{},true,true},{"mode",ParamType::Integer,0}}};
  }
};
} // namespace ds::acceptance
