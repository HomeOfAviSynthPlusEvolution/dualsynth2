#pragma once
#include <dualsynth/staged_video.hpp>
#include <dualsynth/video_bridge.hpp>
#include <atomic>
#include <memory>

namespace ds::acceptance {
inline std::atomic<int> live_stage_instances{0};
struct StageInstanceLifetime {
  StageInstanceLifetime() { ++live_stage_instances; }
  ~StageInstanceLifetime() { --live_stage_instances; }
};
struct StageProbe {
  static constexpr const char* name = "StageProbe";
  static constexpr int input_count = dynamic_video_inputs;
  static constexpr HostRequirements host_requirements{true,11,0};
  static constexpr OutputOrigin output_origin = OutputOrigin::fresh_without_props();
  struct State { int mode; std::shared_ptr<StageInstanceLifetime> lifetime; };
  struct RequestState { int phase = 0; int n = -1; int selected = -1; FrameRef current; };
  static Result<VideoInitStateResult<State>> init(VideoInitContext& ctx) {
    if (ctx.inputs.size() != 4) throw std::invalid_argument("StageProbe requires four clips");
    const auto& in = ctx.inputs[0];
    for (const auto& i : ctx.inputs)
      if (i.width != in.width || i.height != in.height || i.format != in.format || i.num_frames != in.num_frames)
        throw std::invalid_argument("StageProbe requires matching clips");
    const int mode = ctx.params->get_int("mode",0).value();
    if (mode == 9) throw std::runtime_error("stage init: original detail");
    return Result<VideoInitStateResult<State>>::success({
      {in.width,in.height,in.num_frames,in.format,in.fps}, {mode,std::make_shared<StageInstanceLifetime>()}});
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
    const int mode = ctx.state<State>().mode;
    // Synthetic temporal sources: encode both input identity and frame number.
    if (mode >= 10 && mode <= 12) {
      if (ctx.dst.format.color_family != ColorFamily::Gray || ctx.dst.format.sample_format != SampleFormat::UInt8)
        throw std::runtime_error("temporal source requires Gray8");
      const auto& plane = ctx.dst.plane(0);
      for (int y = 0; y < plane.height; ++y)
        std::memset(static_cast<char*>(plane.data) + y * plane.stride_bytes,40 * (mode - 9) + ctx.output_frame,plane.width);
      ctx.dst.properties->set("Stamp",std::vector<std::int64_t>{ctx.output_frame});
      ctx.dst.properties->set("ReferenceFrame",std::vector<std::int64_t>{ctx.output_frame});
    }
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
struct StageForwardBridge : StageProbeBridge {
  static constexpr const char* avs_name = "DSStageForward";
  static constexpr std::size_t parity_source_index = 1;
  static constexpr bool forward_audio = true;
};

struct TemporalStageProbe : StageProbe {
  struct RequestState {
    int phase = 0;
    int n = -1;
    int input = 0;
    int target = -1;
    FrameRef current;
  };
  static int integer(const FrameRef& frame, const char* key) {
    const auto* props = frame.view().properties;
    const auto info = props->inspect(key);
    if (!info || info->type != PropertyType::Integer || info->count != 1)
      throw std::runtime_error("invalid temporal metadata");
    return static_cast<int>(std::get<std::vector<std::int64_t>>(*props->find(key))[0]);
  }
  static Result<VideoStageResult> advance(VideoStageContext& ctx, RequestState& r) {
    if (r.phase++ == 0) {
      r.n = ctx.output_frame;
      ctx.request_frame(0,r.n);
      ctx.request_frame(1,r.n);
      return Result<VideoStageResult>::success(VideoStageResult::RequestFrames);
    }
    if (r.phase == 2) {
      r.current = ctx.frames.get(0,r.n).value().owner;
      const auto vectors = ctx.frames.get(1,r.n).value().owner;
      if (integer(vectors,"Stamp") != r.n) throw std::runtime_error("wrong vectors frame");
      const int delta = integer(vectors,"Delta");
      if (delta < -1 || delta > 1) throw std::runtime_error("invalid temporal delta");
      r.target = r.n + delta;
      r.input = delta < 0 ? 2 : 3;
      if (delta == 0 || r.target < 0 || r.target >= ctx.inputs[0].num_frames) {
        r.input = 0;
        r.target = r.n; // Reuse current; never request a clamped/excluded reference.
      } else {
        ctx.request_frame(0,r.n); // Cross-stage duplicates alongside new work.
        ctx.request_frame(1,r.n);
        ctx.request_frame(r.input,r.target);
        return Result<VideoStageResult>::success(VideoStageResult::RequestFrames);
      }
    }
    ctx.origin = OutputOrigin::copy_from_input(r.input,0);
    ctx.origin.pixel_frame = r.target;
    ctx.origin.prop_frame = r.n;
    return Result<VideoStageResult>::success(VideoStageResult::Ready);
  }
  static Result<VideoProcessResult> process(VideoProcessContext& ctx, RequestState& r) {
    const auto current = ctx.frames.get(0,r.n).value().owner;
    const auto reference = ctx.frames.get(r.input,r.target).value().owner;
    if (r.n != ctx.output_frame || &current.storage() != &r.current.storage() ||
        integer(current,"Stamp") != r.n || integer(reference,"ReferenceFrame") != r.target)
      throw std::runtime_error("temporal frame identity or lifetime mismatch");
    if (r.input == 0 && &reference.storage() != &r.current.storage())
      throw std::runtime_error("zero-delta current frame was reacquired");
    const auto inherited = ctx.dst.properties->find("Stamp");
    if (!inherited || std::get<std::vector<std::int64_t>>(*inherited) != std::vector<std::int64_t>{r.n})
      throw std::runtime_error("temporal output inherited properties from the wrong frame");
    ctx.dst.properties->set("TemporalTarget",std::vector<std::int64_t>{r.target});
    ctx.dst.properties->set("TemporalInput",std::vector<std::int64_t>{r.input});
    return Result<VideoProcessResult>::success({});
  }
};
struct TemporalStageProbeBridge : StageProbeBridge {
  using Core = TemporalStageProbe;
  static constexpr const char* avs_name = "DSTemporalStageProbe";
  static constexpr const char* vs_name = "TemporalStageProbe";
};
} // namespace ds::acceptance
