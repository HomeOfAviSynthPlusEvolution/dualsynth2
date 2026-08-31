#pragma once

#include <vapoursynth/VapourSynth4.h>

#include <dualsynth/format.hpp>
#include <dualsynth/frame.hpp>
#include <dualsynth/param.hpp>
#include <dualsynth/video_bridge.hpp>
#include <dualsynth/video_filter.hpp>

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
    return format.plane_count == 1 ? cfGray : cfYUV;
  }
  return cfUndefined;
}

inline int sample_type(SampleFormat sample_format) {
  return sample_format == SampleFormat::Float32 ? stFloat : stInteger;
}

inline bool query_video_format(
  VideoFormat format,
  VSVideoFormat& output,
  VSCore* core,
  const VSAPI* vsapi
) {
  return vsapi->queryVideoFormat(
    &output,
    color_family(format),
    sample_type(format.sample_format),
    bits_per_sample(format.sample_format),
    format.subsampling_w,
    format.subsampling_h,
    core
  ) != 0;
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

template <class Bridge>
struct VideoFilterData {
  using Filter = typename Bridge::Core;
  static constexpr auto input_count = static_cast<std::size_t>(Filter::input_count);

  std::array<VSNode*, input_count> nodes{};
  std::array<VideoInputInfo, input_count> input_infos{};
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
    const VSAPI* vsapi
  ) : requests_(requests),
      frames_(frames),
      input_infos_(input_infos),
      vsapi_(vsapi) {}

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
        return Result<RequestedVideoFrame>::success(
          RequestedVideoFrame{
            input_index,
            frame_number,
            make_video_frame_view(frame, input_infos_[static_cast<std::size_t>(input_index)].format, vsapi_)
          }
        );
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
};

template <class Bridge>
const VSFrame* execute_process_frame(
  int n,
  VideoFilterData<Bridge>* data,
  Span<const VideoFrameRequest> requests,
  const AcquiredFramesHolder& holder,
  VSFrameContext* frame_ctx,
  VSCore* core,
  const VSAPI* vsapi
) {
  using Filter = typename Bridge::Core;
  const OutputOrigin origin = filter_output_origin<Filter>();

  const VSFrame* pixel_frame = nullptr;
  if (origin.pixels != OutputPixelPolicy::Fresh && origin.pixel_input_index >= 0) {
    for (std::size_t i = 0; i < requests.size(); ++i) {
      if (requests[i].input_index == origin.pixel_input_index &&
          requests[i].frame_number == n) {
        pixel_frame = holder.frames[i];
        break;
      }
    }
  }

  const VSFrame* prop_frame = nullptr;
  if (origin.prop_input_index >= 0) {
    for (std::size_t i = 0; i < requests.size(); ++i) {
      if (requests[i].input_index == origin.prop_input_index &&
          requests[i].frame_number == n) {
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
    vsapi
  );

  const auto result = process_video_filter<Filter>(
    n,
    provider,
    make_mutable_video_frame_view(dst, data->output_format, vsapi),
    data->state
  );

  if (!result.has_value()) {
    vsapi->freeFrame(dst);
    vsapi->setFilterError(result.error().message.c_str(), frame_ctx);
    return nullptr;
  }

  return dst;
}

template <class Bridge>
const VSFrame* VS_CC video_filter_get_frame(
  int n,
  int activation_reason,
  void* instance_data,
  void**,
  VSFrameContext* frame_ctx,
  VSCore* core,
  const VSAPI* vsapi
) {
  using Filter = typename Bridge::Core;
  auto* data = static_cast<VideoFilterData<Bridge>*>(instance_data);

  try {
    std::vector<VideoFrameRequest> requests;
    auto request_result = request_video_filter<Filter>(
      n,
      data->input_infos,
      requests,
      data->state
    );
    if (!request_result.has_value()) {
      vsapi->setFilterError(request_result.error().message.c_str(), frame_ctx);
      return nullptr;
    }

    request_result = request_output_origin_frame(
      filter_output_origin<Filter>(),
      n,
      data->input_infos,
      requests
    );
    if (!request_result.has_value()) {
      vsapi->setFilterError(request_result.error().message.c_str(), frame_ctx);
      return nullptr;
    }

    for (const auto& request : requests) {
      if (request.input_index < 0 ||
          request.input_index >= static_cast<int>(data->nodes.size())) {
        vsapi->setFilterError("DualSynth: video input index is out of range", frame_ctx);
        return nullptr;
      }
    }

    if (activation_reason == arInitial) {
      AcquiredFramesHolder holder{{}, vsapi};
      holder.frames.reserve(requests.size());

      bool all_ready = true;
      for (const auto& request : requests) {
        const VSFrame* frame = vsapi->getFrameFilter(
          request.frame_number,
          data->nodes[static_cast<std::size_t>(request.input_index)],
          frame_ctx
        );
        if (frame == nullptr) {
          all_ready = false;
          break;
        }
        holder.frames.push_back(frame);
      }

      if (all_ready) {
        // Direct-Pass fast path: zero redundant gets, exactly 1 acquisition per frame
        return execute_process_frame<Bridge>(n, data, requests, holder, frame_ctx, core, vsapi);
      }

      // Fallback: asynchronous multi-pass scheduling
      for (const auto& request : requests) {
        vsapi->requestFrameFilter(
          request.frame_number,
          data->nodes[static_cast<std::size_t>(request.input_index)],
          frame_ctx
        );
      }
      return nullptr;
    }

    if (activation_reason != arAllFramesReady) {
      return nullptr;
    }

    AcquiredFramesHolder holder{{}, vsapi};
    holder.frames.reserve(requests.size());
    for (const auto& request : requests) {
      const VSFrame* frame = vsapi->getFrameFilter(
        request.frame_number,
        data->nodes[static_cast<std::size_t>(request.input_index)],
        frame_ctx
      );
      if (frame == nullptr) {
        vsapi->setFilterError("DualSynth: VapourSynth did not provide requested frame in arAllFramesReady", frame_ctx);
        return nullptr;
      }
      holder.frames.push_back(frame);
    }

    return execute_process_frame<Bridge>(n, data, requests, holder, frame_ctx, core, vsapi);
  } catch (const std::exception& error) {
    vsapi->setFilterError(error.what(), frame_ctx);
    return nullptr;
  } catch (...) {
    vsapi->setFilterError("DualSynth: unhandled exception in VapourSynth video wrapper", frame_ctx);
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
  constexpr auto input_count = static_cast<std::size_t>(Filter::input_count);
  auto* data = static_cast<VideoFilterData<Bridge>*>(nullptr);

  try {
    data = new VideoFilterData<Bridge>();
    for (std::size_t i = 0; i < input_count; ++i) {
      int error = 0;
      data->nodes[i] = vsapi->mapGetNode(in, Bridge::vs_input_names[i], 0, &error);
      if (error != peSuccess || data->nodes[i] == nullptr) {
        free_video_filter_nodes(data, vsapi);
        delete data;
        vsapi->mapSetError(out, Bridge::missing_input_error);
        return;
      }

      const VSVideoInfo* input_info = vsapi->getVideoInfo(data->nodes[i]);
      const auto format = make_video_format(input_info->format);
      if (!format.has_value() || !accepts_video_format<Bridge>(format.value())) {
        free_video_filter_nodes(data, vsapi);
        delete data;
        vsapi->mapSetError(out, Bridge::vs_format_error);
        return;
      }

      data->input_infos[i] = VideoInputInfo{
        input_info->width,
        input_info->height,
        input_info->numFrames,
        format.value(),
        FrameRate{input_info->fpsNum, input_info->fpsDen}
      };
    }

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
          params.value(),
          HostKind::VapourSynth
        );
      } else {
        return init_video_filter_instance<Filter>(collected.value(), HostKind::VapourSynth);
      }
    }();
    if (!init_result.has_value()) {
      free_video_filter_nodes(data, vsapi);
      delete data;
      vsapi->mapSetError(out, init_result.error().message.c_str());
      return;
    }

    data->state = std::move(init_result.value().state);

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

    std::array<VSFilterDependency, input_count> dependencies{};
    for (std::size_t i = 0; i < input_count; ++i) {
      dependencies[i] = VSFilterDependency{data->nodes[i], rpStrictSpatial};
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

} // namespace ds::vapoursynth
