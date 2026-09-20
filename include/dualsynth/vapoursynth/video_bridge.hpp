#pragma once

#include <vapoursynth/VapourSynth4.h>

#include <dualsynth/format.hpp>
#include <dualsynth/frame.hpp>
#include <dualsynth/detail/native_frame.hpp>
#include <dualsynth/param.hpp>
#include <dualsynth/video_bridge.hpp>
#include <dualsynth/video_filter.hpp>
#include <dualsynth/staged_video.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace ds::vapoursynth {

template <class Filter, class = void>
struct filter_has_output_origin : std::false_type {};

template <class Filter>
struct filter_has_output_origin<Filter, std::void_t<decltype(Filter::output_origin)>> : std::true_type {};

template <class Filter>
constexpr OutputOrigin filter_output_origin() {
  if constexpr (filter_has_output_origin<Filter>::value) {
    return Filter::output_origin;
  } else {
    return OutputOrigin::fresh();
  }
}

inline int color_family(VideoFormat format) {
  switch (format.color_family) {
  case ColorFamily::Gray:
    return cfGray;
  case ColorFamily::Rgb:
    return cfRGB;
  case ColorFamily::Yuv:
    return cfYUV;
  }
  return cfUndefined;
}

inline int sample_type(SampleFormat sample_format) {
  return sample_format == SampleFormat::Float32 ? stFloat : stInteger;
}

inline void validate_output_video_info(const VideoOutputInfo& output) {
  try {
    validate_frame_dimensions(output.format, output.width, output.height);
  } catch (const std::invalid_argument&) {
    throw std::invalid_argument("DualSynth: invalid VapourSynth output dimensions or format");
  }
  if (output.num_frames <= 0)
    throw std::invalid_argument("DualSynth: VapourSynth output frame count must be positive");
  const auto fps = output.fps;
  // VS uses 0/0 for an unknown or variable frame rate.
  if (fps.numerator < 0 || fps.denominator < 0 ||
      ((fps.numerator == 0) != (fps.denominator == 0)))
    throw std::invalid_argument("DualSynth: invalid VapourSynth output frame rate");
}

inline bool query_video_format(
  VideoFormat format,
  VSVideoFormat& output,
  VSCore* core,
  const VSAPI* vsapi
) {
  if (!is_supported_video_format(format).has_value() || format.plane_count == 4)
    return false; // Native VS video formats do not contain an alpha plane.
  if (!vsapi->queryVideoFormat(
    &output,
    color_family(format),
    sample_type(format.sample_format),
    bits_per_sample(format.sample_format),
    format.subsampling_w,
    format.subsampling_h,
    core
  )) return false;
  return output.colorFamily == color_family(format) &&
         output.sampleType == sample_type(format.sample_format) &&
         output.bitsPerSample == bits_per_sample(format.sample_format) &&
         output.bytesPerSample == bytes_per_sample(format.sample_format) &&
         output.numPlanes == format.plane_count &&
         output.subSamplingW == format.subsampling_w && output.subSamplingH == format.subsampling_h;
}

inline Result<VideoFormat> make_video_format(const VSVideoFormat& format) {
  ColorFamily color{};
  switch (format.colorFamily) {
  case cfGray:
    color = ColorFamily::Gray;
    break;
  case cfYUV:
    color = ColorFamily::Yuv;
    break;
  case cfRGB:
    color = ColorFamily::Rgb;
    break;
  default:
    return Result<VideoFormat>::failure({
      ErrorCode::UnsupportedFormat,
      "unsupported VapourSynth color family"
    });
  }

  if (format.sampleType != stInteger && format.sampleType != stFloat) {
    return Result<VideoFormat>::failure({
      ErrorCode::UnsupportedFormat,
      "unsupported VapourSynth sample type"
    });
  }

  return ds::make_video_format(
    color,
    format.sampleType == stFloat,
    format.bitsPerSample,
    format.numPlanes,
    format.subSamplingW,
    format.subSamplingH
  );
}

inline VideoFrameView make_video_frame_view(
  const VSFrame* frame,
  VideoFormat format,
  const VSAPI* vsapi
) {
  std::array<PlaneView, 4> planes{};
  for (int plane = 0; plane < format.plane_count; ++plane) {
    planes[static_cast<std::size_t>(plane)] = PlaneView{
      vsapi->getReadPtr(frame, plane),
      vsapi->getStride(frame, plane),
      vsapi->getFrameWidth(frame, plane),
      vsapi->getFrameHeight(frame, plane)
    };
  }
  return VideoFrameView{format, format.plane_count, planes};
}

inline MutableVideoFrameView make_mutable_video_frame_view(
  VSFrame* frame,
  VideoFormat format,
  const VSAPI* vsapi
) {
  std::array<MutablePlaneView, 4> planes{};
  for (int plane = 0; plane < format.plane_count; ++plane) {
    planes[static_cast<std::size_t>(plane)] = MutablePlaneView{
      vsapi->getWritePtr(frame, plane),
      vsapi->getStride(frame, plane),
      vsapi->getFrameWidth(frame, plane),
      vsapi->getFrameHeight(frame, plane)
    };
  }
  return MutableVideoFrameView{format, format.plane_count, planes};
}

#include <dualsynth/detail/vs_frame_traits.inc>

template <class Bridge>
struct VideoFilterData {
  using Filter = typename Bridge::Core;
  static constexpr auto input_count = static_cast<std::size_t>(Filter::input_count);

  VideoInputStorage<Filter, VSNode*> nodes{};
  VideoInputStorage<Filter, VideoInputInfo> input_infos{};
  std::vector<VideoInputGroup> input_groups;
  VSVideoInfo video_info{};
  VideoFormat output_format{ColorFamily::Gray, SampleFormat::UInt8, 1, 0, 0};
  VideoFilterState<Filter> state{};
};

template <class Bridge>
void free_video_filter_nodes(VideoFilterData<Bridge>* data, const VSAPI* vsapi) {
  if (data == nullptr) {
    return;
  }

  for (VSNode* node : data->nodes) {
    if (node != nullptr) {
      vsapi->freeNode(node);
    }
  }
}

inline VSFrame* new_output_frame(
  const VSVideoInfo& video_info,
  VideoFormat output_format,
  OutputOrigin origin,
  const VSFrame* pixel_frame,
  const VSFrame* prop_frame,
  VSCore* core,
  const VSAPI* vsapi
) {
  if (origin.pixels == OutputPixelPolicy::Fresh || pixel_frame == nullptr) {
    return vsapi->newVideoFrame(
      &video_info.format,
      video_info.width,
      video_info.height,
      prop_frame,
      core
    );
  }

  std::array<const VSFrame*, 4> plane_sources{};
  std::array<int, 4> planes{};
  for (int plane = 0; plane < output_format.plane_count; ++plane) {
    plane_sources[static_cast<std::size_t>(plane)] = pixel_frame;
    planes[static_cast<std::size_t>(plane)] = plane;
  }

  return vsapi->newVideoFrame2(
    &video_info.format,
    video_info.width,
    video_info.height,
    plane_sources.data(),
    planes.data(),
    prop_frame,
    core
  );
}

struct AcquiredFramesHolder {
  std::vector<const VSFrame*> frames;
  const VSAPI* vsapi = nullptr;

  ~AcquiredFramesHolder() {
    if (vsapi != nullptr) {
      for (const VSFrame* frame : frames) {
        if (frame != nullptr) {
          vsapi->freeFrame(frame);
        }
      }
    }
  }
};

template <class Bridge>
class PreloadedVideoFrameProvider final : public ds::VideoFrameProvider {
public:
  PreloadedVideoFrameProvider(
    Span<const VideoFrameRequest> requests,
    Span<const VSFrame* const> frames,
    Span<const VideoInputInfo> input_infos,
    const VSAPI* vsapi, VSCore* core = nullptr
  ) : requests_(requests),
      frames_(frames),
      input_infos_(input_infos),
      vsapi_(vsapi), core_(core) {}

  Result<RequestedVideoFrame> get(int input_index, int frame_number) override {
    for (std::size_t i = 0; i < requests_.size(); ++i) {
      if (requests_[i].input_index == input_index &&
          requests_[i].frame_number == frame_number) {
        const VSFrame* frame = frames_[i];
        if (frame == nullptr) {
          return Result<RequestedVideoFrame>::failure(
            Error{ErrorCode::HostError, "DualSynth: VapourSynth did not provide the requested frame"}
          );
        }
        FrameRef owner;
        auto view = make_video_frame_view(frame, input_infos_[static_cast<std::size_t>(input_index)].format, vsapi_);
        if constexpr (FilterRequirements<typename Bridge::Core>::value.frame_services) {
          FrameTraits traits{vsapi_, core_};
          owner = detail::NativeFrame<FrameTraits>::adopt(traits, traits.clone(frame));
          view = owner.view();
        }
        return Result<RequestedVideoFrame>::success({input_index, frame_number, view, std::move(owner)});
      }
    }

    return Result<RequestedVideoFrame>::failure(
      Error{ErrorCode::InvalidArgument, "DualSynth: requested video frame was not declared"}
    );
  }

private:
  Span<const VideoFrameRequest> requests_;
  Span<const VSFrame* const> frames_;
  Span<const VideoInputInfo> input_infos_;
  const VSAPI* vsapi_;
  VSCore* core_;
};

class InitFrameProvider final : public ds::VideoFrameProvider {
public:
  InitFrameProvider(Span<VSNode*> nodes, Span<const VideoInputInfo> infos, FrameTraits traits, bool services)
    : nodes_(nodes), infos_(infos), traits_(traits), services_(services), holder_{{}, traits.api} {}
  Result<RequestedVideoFrame> get(int input, int n) override {
    if (input < 0 || static_cast<std::size_t>(input) >= nodes_.size() || n < 0 || n >= infos_[input].num_frames)
      return Result<RequestedVideoFrame>::failure({ErrorCode::InvalidArgument, "DualSynth: invalid initialization frame request"});
    char error[1024]{};
    const VSFrame* frame = traits_.api->getFrame(n, nodes_[input], error, sizeof(error));
    if (!frame) return Result<RequestedVideoFrame>::failure({ErrorCode::HostError, error});
    if (services_) {
      auto owner = detail::NativeFrame<FrameTraits>::adopt(traits_, frame);
      auto view = owner.view();
      return Result<RequestedVideoFrame>::success({input,n,view,std::move(owner)});
    }
    try { holder_.frames.push_back(frame); }
    catch (...) { traits_.api->freeFrame(frame); throw; }
    return Result<RequestedVideoFrame>::success({input,n,make_video_frame_view(frame,infos_[input].format,traits_.api)});
  }
private:
  Span<VSNode*> nodes_;
  Span<const VideoInputInfo> infos_;
  FrameTraits traits_;
  bool services_;
  AcquiredFramesHolder holder_;
};

struct VideoRequestPlan {
  std::vector<VideoFrameRequest> requests;
  OutputOrigin origin;
};

template <class Bridge>
const VSFrame* execute_process_frame(
  int n,
  VideoFilterData<Bridge>* data,
  Span<const VideoFrameRequest> requests,
  const AcquiredFramesHolder& holder,
  OutputOrigin origin,
  VSFrameContext* frame_ctx,
  VSCore* core,
  const VSAPI* vsapi
) {
  using Filter = typename Bridge::Core;

  const VSFrame* pixel_frame = nullptr;
  if (origin.pixels != OutputPixelPolicy::Fresh && origin.pixel_input_index >= 0) {
    for (std::size_t i = 0; i < requests.size(); ++i) {
      if (requests[i].input_index == origin.pixel_input_index &&
          requests[i].frame_number == (origin.pixel_frame < 0 ? n : origin.pixel_frame)) {
        pixel_frame = holder.frames[i];
        break;
      }
    }
  }

  const VSFrame* prop_frame = nullptr;
  if (origin.prop_input_index >= 0) {
    for (std::size_t i = 0; i < requests.size(); ++i) {
      if (requests[i].input_index == origin.prop_input_index &&
          requests[i].frame_number == (origin.prop_frame < 0 ? n : origin.prop_frame)) {
        prop_frame = holder.frames[i];
        break;
      }
    }
  }

  VSFrame* dst = new_output_frame(
    data->video_info,
    data->output_format,
    origin,
    pixel_frame,
    prop_frame,
    core,
    vsapi
  );

  if (dst == nullptr) {
    vsapi->setFilterError("DualSynth: failed to allocate VapourSynth output frame", frame_ctx);
    return nullptr;
  }

  PreloadedVideoFrameProvider<Bridge> provider(
    requests,
    holder.frames,
    data->input_infos,
    vsapi, core
  );

  std::unique_ptr<const VSFrame, decltype(vsapi->freeFrame)> output_guard(dst, vsapi->freeFrame);
  const VSFrame* property_frame = dst;
  FrameTraits traits{vsapi, core};
  detail::NativeProperties<FrameTraits> properties(traits, property_frame, true);
  detail::NativeFrameFactory<FrameTraits> factory(traits);
  auto view = make_mutable_video_frame_view(dst, data->output_format, vsapi);
  if constexpr (FilterRequirements<Filter>::value.frame_services) view.properties = &properties;
  const auto result = process_video_filter<Filter>(
    n,
    provider,
    view,
    data->state,
    FilterRequirements<Filter>::value.frame_services ? &factory : nullptr
  );

  if (!result.has_value()) {
    vsapi->setFilterError(result.error().message.c_str(), frame_ctx);
    return nullptr;
  }

  output_guard.release();
  return dst;
}

template <class Bridge>
const VSFrame* staged_video_filter_get_frame(
  int n, int activation, VideoFilterData<Bridge>* data, void** frame_data,
  VSFrameContext* frame_ctx, VSCore* core, const VSAPI* api
) {
  using Filter = typename Bridge::Core;
  using Request = StagedVideoRequest<Filter>;
  if (activation == arError) {
    delete static_cast<Request*>(*frame_data);
    *frame_data = nullptr;
    return nullptr; // Preserve the host's original upstream error.
  }
  if (activation != arInitial && activation != arAllFramesReady) return nullptr;
  std::unique_ptr<Request> request;
  try {
    FrameTraits traits{api,core};
    if (activation == arInitial) {
      request = std::make_unique<Request>(n,data->state);
    } else {
      request.reset(static_cast<Request*>(*frame_data));
      *frame_data = nullptr;
      if (!request) throw std::logic_error("DualSynth: missing staged request");
      for (const auto& r : request->pending()) {
        const auto* native = api->getFrameFilter(r.frame_number,data->nodes[r.input_index],frame_ctx);
        if (!native) throw std::runtime_error("DualSynth: staged frame was not provided");
        auto owner = detail::NativeFrame<FrameTraits>::adopt(traits,native);
        auto view = owner.view();
        request->accept({r.input_index,r.frame_number,view,std::move(owner)});
      }
    }
    if (!request->advance(data->input_infos,data->state)) {
      for (const auto& r : request->pending())
        api->requestFrameFilter(r.frame_number,data->nodes[r.input_index],frame_ctx);
      *frame_data = request.release();
      return nullptr;
    }
    detail::NativeFrameFactory<FrameTraits> factory(traits);
    const auto& vi = data->video_info;
    auto frame = request->finish({vi.width,vi.height,vi.numFrames,data->output_format,{}},data->input_infos,data->state,factory);
    return traits.clone(dynamic_cast<const detail::NativeFrame<FrameTraits>&>(frame.storage()).frame());
  } catch (const std::exception& error) {
    api->setFilterError(error.what(),frame_ctx);
  } catch (...) {
    api->setFilterError("DualSynth: unhandled exception in staged video filter",frame_ctx);
  }
  return nullptr;
}

template <class Bridge>
const VSFrame* VS_CC video_filter_get_frame(
  int n, int activation_reason, void* instance_data, void** frame_data,
  VSFrameContext* frame_ctx, VSCore* core, const VSAPI* vsapi
) {
  using Filter = typename Bridge::Core;
  auto* data = static_cast<VideoFilterData<Bridge>*>(instance_data);
  if constexpr (HasVideoStages<Filter>::value) {
    return staged_video_filter_get_frame<Bridge>(n,activation_reason,data,frame_data,frame_ctx,core,vsapi);
  } else {
  if (activation_reason == arError) {
    delete static_cast<VideoRequestPlan*>(*frame_data);
    *frame_data = nullptr;
    return nullptr;
  }
  if (activation_reason != arInitial && activation_reason != arAllFramesReady) return nullptr;
  std::unique_ptr<VideoRequestPlan> plan;
  try {
    if (activation_reason == arInitial) {
      plan = std::make_unique<VideoRequestPlan>();
      auto result = request_video_filter<Filter>(n, data->input_infos, plan->requests, data->state);
      if (!result.has_value()) throw std::runtime_error(result.error().message);
      plan->origin = resolve_output_origin<Filter>(n, data->state);
      result = request_output_origin_frame(plan->origin, n, data->input_infos, plan->requests);
      if (!result.has_value()) throw std::runtime_error(result.error().message);
      for (const auto& request : plan->requests) {
        if (request.input_index < 0 || static_cast<std::size_t>(request.input_index) >= data->nodes.size())
          throw std::invalid_argument("DualSynth: video input index is out of range");
        if (video_request_pattern<Filter>(request.input_index, data->state) == VideoRequestPattern::StrictSpatial && request.frame_number != n)
          throw std::invalid_argument("DualSynth: temporal request violates strict spatial dependency");
      }
      if (plan->requests.empty()) {
        AcquiredFramesHolder holder{{}, vsapi};
        return execute_process_frame<Bridge>(n,data,plan->requests,holder,plan->origin,frame_ctx,core,vsapi);
      }
      // Standard VS4 contract: getFrameFilter is legal only after requesting frames.
      for (const auto& request : plan->requests)
        vsapi->requestFrameFilter(request.frame_number, data->nodes[request.input_index], frame_ctx);
      *frame_data = plan.release();
      return nullptr;
    }
    plan.reset(static_cast<VideoRequestPlan*>(*frame_data));
    *frame_data = nullptr;
    if (!plan) throw std::logic_error("DualSynth: missing frame request plan");
    AcquiredFramesHolder holder{{}, vsapi};
    holder.frames.reserve(plan->requests.size());
    for (const auto& request : plan->requests) {
      const VSFrame* frame = vsapi->getFrameFilter(request.frame_number, data->nodes[request.input_index], frame_ctx);
      if (!frame) throw std::runtime_error("DualSynth: requested frame was not provided");
      holder.frames.push_back(frame);
    }
    return execute_process_frame<Bridge>(n,data,plan->requests,holder,plan->origin,frame_ctx,core,vsapi);
  } catch (const std::exception& error) {
    vsapi->setFilterError(error.what(), frame_ctx);
  } catch (...) {
    vsapi->setFilterError("DualSynth: unhandled exception in VapourSynth video wrapper", frame_ctx);
  }
  return nullptr;
  }
}

template <class Bridge>
void set_create_error(VSMap* out, const VSAPI* vsapi, const char* message) {
  vsapi->mapSetError(out, message);
}

template <class Bridge>
void VS_CC video_filter_free(void* instance_data, VSCore*, const VSAPI* vsapi) {
  auto* data = static_cast<VideoFilterData<Bridge>*>(instance_data);
  free_video_filter_nodes(data, vsapi);
  delete data;
}

template <class Bridge, class Format, class = void>
struct bridge_has_accepts_video_format : std::false_type {};

template <class Bridge, class Format>
struct bridge_has_accepts_video_format<
  Bridge,
  Format,
  std::void_t<decltype(Bridge::accepts_video_format(std::declval<Format>()))>
> : std::true_type {};

template <class Bridge>
bool accepts_video_format(VideoFormat format) {
  if constexpr (bridge_has_accepts_video_format<Bridge, VideoFormat>::value) {
    return Bridge::accepts_video_format(format);
  } else {
    return true;
  }
}

template <class Bridge, class = void>
struct bridge_has_descriptor : std::false_type {};

template <class Bridge>
struct bridge_has_descriptor<
  Bridge,
  std::void_t<decltype(Bridge::descriptor())>
> : std::true_type {};

inline Result<ParamValues> read_params(
  const VSMap* in,
  const FilterDescriptor& descriptor,
  const VSAPI* vsapi
);

template <DS_CONCEPT_VIDEO_BRIDGE Bridge>
void create_video_filter_bridge(
  const VSMap* in,
  VSMap* out,
  VSCore* core,
  const VSAPI* vsapi
) {
  using Filter = typename Bridge::Core;
  auto* data = static_cast<VideoFilterData<Bridge>*>(nullptr);

  try {
    check_host_requirements<Filter>(vsapi);
    data = new VideoFilterData<Bridge>();
    std::size_t flat = 0;
    for (const auto& spec : bridge_clip_inputs<Bridge>(true)) {
      int count = vsapi->mapNumElements(in, spec.name.c_str());
      const bool provided = count >= 0;
      if (count < 0 && spec.optional) count = 0;
      if (count < 0 || (!spec.optional && count == 0) || (!spec.array && count > 1))
        throw std::invalid_argument(Bridge::missing_input_error);
      data->input_groups.push_back({spec.name, flat, static_cast<std::size_t>(count),provided});
      for (int element = 0; element < count; ++element, ++flat) {
        if constexpr (Filter::input_count == dynamic_video_inputs) {
          data->nodes.push_back(nullptr);
          data->input_infos.push_back({});
        }
        int error = 0;
        data->nodes[flat] = vsapi->mapGetNode(in, spec.name.c_str(), element, &error);
        if (error || !data->nodes[flat]) throw std::invalid_argument(Bridge::missing_input_error);
        const VSVideoInfo* vi = vsapi->getVideoInfo(data->nodes[flat]);
        if (!vi || vi->width <= 0 || vi->height <= 0 || vi->numFrames <= 0)
          throw std::invalid_argument("DualSynth: input must have constant video format and dimensions");
        auto format = make_video_format(vi->format);
        if (!format.has_value() || !accepts_video_format<Bridge>(format.value()))
          throw std::invalid_argument(Bridge::vs_format_error);
        data->input_infos[flat] = {vi->width,vi->height,vi->numFrames,format.value(),{vi->fpsNum,vi->fpsDen}};
      }
    }
    if (flat == 0) throw std::invalid_argument("DualSynth: at least one input clip is required");
    FrameTraits traits{vsapi, core};
    detail::NativeFrameFactory<FrameTraits> factory(traits);
    InitFrameProvider init_frames(data->nodes, data->input_infos, traits, FilterRequirements<Filter>::value.frame_services);

    const auto collected = collect_video_input_infos<Filter>(data->input_infos);
    if (!collected.has_value()) {
      free_video_filter_nodes(data, vsapi);
      delete data;
      vsapi->mapSetError(out, collected.error().message.c_str());
      return;
    }

    auto init_result = [&]() -> Result<VideoFilterInstance<Filter>> {
      if constexpr (bridge_has_descriptor<Bridge>::value) {
        auto params = read_params(in, Bridge::descriptor(), vsapi);
        if (!params.has_value()) {
          return Result<VideoFilterInstance<Filter>>::failure(params.error());
        }
        return init_video_filter_instance<Filter>(
          collected.value(),
          &params.value(), {}, {}, HostKind::VapourSynth, &init_frames,
          FilterRequirements<Filter>::value.frame_services ? &factory : nullptr, data->input_groups
        );
      } else {
        return init_video_filter_instance<Filter>(collected.value(), nullptr, {}, {}, HostKind::VapourSynth, &init_frames,
          FilterRequirements<Filter>::value.frame_services ? &factory : nullptr, data->input_groups);
      }
    }();
    if (!init_result.has_value()) {
      free_video_filter_nodes(data, vsapi);
      delete data;
      vsapi->mapSetError(out, init_result.error().message.c_str());
      return;
    }

    data->state = std::move(init_result.value().state);

    validate_output_video_info(init_result.value().output);

    VSVideoFormat output_format{};
    if (!query_video_format(init_result.value().output.format, output_format, core, vsapi)) {
      free_video_filter_nodes(data, vsapi);
      delete data;
      vsapi->mapSetError(out, "DualSynth: unsupported VapourSynth output format");
      return;
    }

    const VSVideoInfo* base_info = vsapi->getVideoInfo(data->nodes[0]);
    data->video_info = *base_info;
    data->video_info.format = output_format;
    data->video_info.width = init_result.value().output.width;
    data->video_info.height = init_result.value().output.height;
    data->video_info.numFrames = init_result.value().output.num_frames;
    data->video_info.fpsNum = init_result.value().output.fps.numerator;
    data->video_info.fpsDen = init_result.value().output.fps.denominator;
    data->output_format = init_result.value().output.format;

    std::vector<VSFilterDependency> dependencies(data->nodes.size());
    for (std::size_t i = 0; i < data->nodes.size(); ++i) {
      auto pattern = video_request_pattern<Filter>(static_cast<int>(i), data->state);
      int native = pattern == VideoRequestPattern::StrictSpatial ? rpStrictSpatial :
                   pattern == VideoRequestPattern::NoFrameReuse ? rpNoFrameReuse : rpGeneral;
      dependencies[i] = VSFilterDependency{data->nodes[i], native};
    }

    vsapi->createVideoFilter(
      out,
      Bridge::vs_name,
      &data->video_info,
      video_filter_get_frame<Bridge>,
      video_filter_free<Bridge>,
      fmParallel,
      dependencies.data(),
      static_cast<int>(dependencies.size()),
      data,
      core
    );
    data = nullptr;
  } catch (const std::exception& error) {
    free_video_filter_nodes(data, vsapi);
    delete data;
    set_create_error<Bridge>(out, vsapi, error.what());
  } catch (...) {
    free_video_filter_nodes(data, vsapi);
    delete data;
    set_create_error<Bridge>(
      out,
      vsapi,
      "DualSynth: unhandled exception in VapourSynth video creation"
    );
  }
}

inline ParamValues read_optional_int_params(
  const VSMap* in,
  Span<const char* const> names,
  const VSAPI* vsapi
) {
  ParamValues values{};
  for (const char* name : names) {
    int error = 0;
    const int value = vsapi->mapGetIntSaturated(in, name, 0, &error);
    if (error == peSuccess) {
      values.entries.push_back(ParamEntry{name, ParamValue{value}});
    }
  }
  return values;
}

inline Result<ParamValues> read_params(
  const VSMap* in,
  const FilterDescriptor& descriptor,
  const VSAPI* vsapi
) {
  auto validation = validate_filter_descriptor(descriptor);
  if (!validation.has_value()) {
    return Result<ParamValues>::failure(validation.error());
  }

  ParamValues values{};
  for (const auto& param : descriptor.params) {
    if (!param.vs_enabled || param.type == ParamType::Clip) {
      continue;
    }

    const int element_count = vsapi->mapNumElements(in, param.name.c_str());
    if (element_count < 0) {
      if (param.required) {
        return Result<ParamValues>::failure({
          ErrorCode::InvalidArgument,
          "missing required VapourSynth parameter '" + param.name + "'"
        });
      }
      continue;
    }

    if (param.is_array) {
      switch (param.type) {
      case ParamType::Integer: {
        std::vector<std::int64_t> output;
        output.reserve(static_cast<std::size_t>(element_count));
        for (int i = 0; i < element_count; ++i) {
          int error = 0;
          const std::int64_t value = vsapi->mapGetInt(in, param.name.c_str(), i, &error);
          if (error != peSuccess) {
            return Result<ParamValues>::failure({
              ErrorCode::InvalidArgument,
              "VapourSynth parameter '" + param.name + "' must be an integer array"
            });
          }
          output.push_back(value);
        }
        values.entries.push_back(ParamEntry{param.name, ParamValue{std::move(output)}});
        break;
      }
      case ParamType::Float: {
        std::vector<double> output;
        output.reserve(static_cast<std::size_t>(element_count));
        for (int i = 0; i < element_count; ++i) {
          int error = 0;
          const double value = vsapi->mapGetFloat(in, param.name.c_str(), i, &error);
          if (error != peSuccess) {
            return Result<ParamValues>::failure({
              ErrorCode::InvalidArgument,
              "VapourSynth parameter '" + param.name + "' must be a float array"
            });
          }
          output.push_back(value);
        }
        values.entries.push_back(ParamEntry{param.name, ParamValue{std::move(output)}});
        break;
      }
      case ParamType::Boolean: {
        std::vector<bool> output;
        output.reserve(static_cast<std::size_t>(element_count));
        for (int i = 0; i < element_count; ++i) {
          int error = 0;
          const std::int64_t value = vsapi->mapGetInt(in, param.name.c_str(), i, &error);
          if (error != peSuccess) {
            return Result<ParamValues>::failure({
              ErrorCode::InvalidArgument,
              "VapourSynth parameter '" + param.name + "' must be a boolean array"
            });
          }
          output.push_back(value != 0);
        }
        values.entries.push_back(ParamEntry{param.name, ParamValue{std::move(output)}});
        break;
      }
      case ParamType::String: {
        std::vector<std::string> output;
        output.reserve(static_cast<std::size_t>(element_count));
        for (int i = 0; i < element_count; ++i) {
          int error = 0;
          const char* value = vsapi->mapGetData(in, param.name.c_str(), i, &error);
          if (error != peSuccess) {
            return Result<ParamValues>::failure({
              ErrorCode::InvalidArgument,
              "VapourSynth parameter '" + param.name + "' must be a data array"
            });
          }
          output.emplace_back(value);
        }
        values.entries.push_back(ParamEntry{param.name, ParamValue{std::move(output)}});
        break;
      }
      case ParamType::Clip:
        break;
      }
      continue;
    }

    int error = 0;
    switch (param.type) {
    case ParamType::Integer:
      values.entries.push_back(ParamEntry{
        param.name,
        ParamValue{vsapi->mapGetInt(in, param.name.c_str(), 0, &error)}
      });
      break;
    case ParamType::Float:
      values.entries.push_back(ParamEntry{
        param.name,
        ParamValue{vsapi->mapGetFloat(in, param.name.c_str(), 0, &error)}
      });
      break;
    case ParamType::Boolean:
      values.entries.push_back(ParamEntry{
        param.name,
        ParamValue{vsapi->mapGetInt(in, param.name.c_str(), 0, &error) != 0}
      });
      break;
    case ParamType::String:
      if (const char* value = vsapi->mapGetData(in, param.name.c_str(), 0, &error);
          error == peSuccess) {
        values.entries.push_back(ParamEntry{
          param.name,
          ParamValue{std::string(value ? value : "")}
        });
      }
      break;
    case ParamType::Clip:
      break;
    }

    if (error != peSuccess) {
      return Result<ParamValues>::failure({
        ErrorCode::InvalidArgument,
        "VapourSynth parameter '" + param.name + "' has the wrong type"
      });
    }
  }

  return Result<ParamValues>::success(std::move(values));
}

template <DS_CONCEPT_VIDEO_BRIDGE Bridge, class Creator>
decltype(auto) create_video_filter_bridge(Creator&& creator) {
  return std::forward<Creator>(creator).template operator()<typename Bridge::Core>(
    Bridge::vs_input_names,
    Bridge::missing_input_error,
    Bridge::vs_format_error
  );
}


template<class Bridge>
void create_video_filter_bundle(Span<const VSMap* const> calls, VSMap* out, VSCore* core, const VSAPI* api) {
  try {
    std::vector<std::unique_ptr<VSNode, decltype(api->freeNode)>> nodes;
    nodes.reserve(calls.size());
    for (std::size_t index = 0; index < calls.size(); ++index) {
      try {
        std::unique_ptr<VSMap, decltype(api->freeMap)> result(api->createMap(),api->freeMap);
        if (!result) throw std::bad_alloc();
        create_video_filter_bridge<Bridge>(calls[index],result.get(),core,api);
        if (const char* error = api->mapGetError(result.get())) throw std::runtime_error(error);
        int error = 0;
        std::unique_ptr<VSNode, decltype(api->freeNode)> node(api->mapGetNode(result.get(),"clip",0,&error),api->freeNode);
        if (error || !node) throw std::runtime_error("DualSynth: bundle member did not return a clip");
        nodes.push_back(std::move(node));
      } catch (const std::exception& error) {
        throw std::runtime_error("DualSynth: bundle member[" + std::to_string(index) + "]: " + error.what());
      } catch (...) {
        throw std::runtime_error("DualSynth: bundle member[" + std::to_string(index) + "]: unhandled exception");
      }
    }
    api->mapDeleteKey(out,"clip");
    if (nodes.empty()) {
      if (api->mapSetEmpty(out,"clip",ptVideoNode)) throw std::runtime_error("DualSynth: cannot create empty clip array");
    }
    for (const auto& node : nodes)
      if (api->mapSetNode(out,"clip",node.get(),maAppend)) throw std::runtime_error("DualSynth: cannot append bundle clip");
  } catch (const std::exception& error) { api->mapSetError(out,error.what()); }
  catch (...) { api->mapSetError(out,"DualSynth: bundle creation failed"); }
}

} // namespace ds::vapoursynth
