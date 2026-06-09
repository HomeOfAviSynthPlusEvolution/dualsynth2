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
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ds::vapoursynth {

template <class Filter>
constexpr OutputOrigin filter_output_origin() {
  if constexpr (requires { Filter::output_origin; }) {
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

template <std::size_t InputCount>
class VideoFrameProvider final : public ds::VideoFrameProvider {
public:
  VideoFrameProvider(
    std::span<VSNode*> nodes,
    std::span<const VideoInputInfo> input_infos,
    VSFrameContext* frame_ctx,
    const VSAPI* vsapi
  ) : nodes_(nodes),
      input_infos_(input_infos),
      frame_ctx_(frame_ctx),
      vsapi_(vsapi) {}

  ~VideoFrameProvider() override {
    for (const VSFrame* frame : frames_) {
      if (frame != nullptr) {
        vsapi_->freeFrame(frame);
      }
    }
  }

  Result<RequestedVideoFrame> get(int input_index, int frame_number) override {
    if (input_index < 0 || input_index >= static_cast<int>(nodes_.size())) {
      return Result<RequestedVideoFrame>::failure(
        Error{ErrorCode::InvalidArgument, "DualSynth: video input index is out of range"}
      );
    }

    const auto index = static_cast<std::size_t>(input_index);
    const VSFrame* frame = vsapi_->getFrameFilter(frame_number, nodes_[index], frame_ctx_);
    if (frame == nullptr) {
      return Result<RequestedVideoFrame>::failure(
        Error{ErrorCode::HostError, "DualSynth: VapourSynth did not provide the requested frame"}
      );
    }

    frames_.push_back(frame);
    return Result<RequestedVideoFrame>::success(
      RequestedVideoFrame{
        input_index,
        frame_number,
        make_video_frame_view(frame, input_infos_[index].format, vsapi_)
      }
    );
  }

private:
  std::span<VSNode*> nodes_;
  std::span<const VideoInputInfo> input_infos_;
  VSFrameContext* frame_ctx_;
  const VSAPI* vsapi_;
  std::vector<const VSFrame*> frames_;
};

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

template <class Bridge>
const VSFrame* output_origin_frame(
  OutputOrigin origin,
  int output_frame,
  const VideoFilterData<Bridge>* data,
  VSFrameContext* frame_ctx,
  const VSAPI* vsapi
) {
  if (origin.kind == OutputOriginKind::Fresh) {
    return nullptr;
  }

  return vsapi->getFrameFilter(
    output_frame,
    data->nodes[static_cast<std::size_t>(origin.input_index)],
    frame_ctx
  );
}

inline VSFrame* new_output_frame(
  const VSVideoInfo& video_info,
  VideoFormat output_format,
  OutputOrigin origin,
  const VSFrame* origin_frame,
  VSCore* core,
  const VSAPI* vsapi
) {
  if (origin.kind == OutputOriginKind::Fresh || origin_frame == nullptr) {
    return vsapi->newVideoFrame(
      &video_info.format,
      video_info.width,
      video_info.height,
      nullptr,
      core
    );
  }

  std::array<const VSFrame*, 4> plane_sources{};
  std::array<int, 4> planes{};
  for (int plane = 0; plane < output_format.plane_count; ++plane) {
    plane_sources[static_cast<std::size_t>(plane)] = origin_frame;
    planes[static_cast<std::size_t>(plane)] = plane;
  }

  return vsapi->newVideoFrame2(
    &video_info.format,
    video_info.width,
    video_info.height,
    plane_sources.data(),
    planes.data(),
    origin_frame,
    core
  );
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
  const VSFrame* origin_frame = nullptr;
  VSFrame* dst = nullptr;

  try {
    if (activation_reason == arInitial) {
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

    const OutputOrigin origin = filter_output_origin<Filter>();
    origin_frame = output_origin_frame(origin, n, data, frame_ctx, vsapi);
    dst = new_output_frame(
      data->video_info,
      data->output_format,
      origin,
      origin_frame,
      core,
      vsapi
    );

    if (origin_frame != nullptr) {
      vsapi->freeFrame(origin_frame);
      origin_frame = nullptr;
    }

    if (dst == nullptr) {
      vsapi->setFilterError("DualSynth: failed to allocate VapourSynth output frame", frame_ctx);
      return nullptr;
    }

    VideoFrameProvider<VideoFilterData<Bridge>::input_count> provider(
      data->nodes,
      data->input_infos,
      frame_ctx,
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
      dst = nullptr;
      vsapi->setFilterError(result.error().message.c_str(), frame_ctx);
      return nullptr;
    }

    VSFrame* output = dst;
    dst = nullptr;
    return output;
  } catch (const std::exception& error) {
    if (origin_frame != nullptr) {
      vsapi->freeFrame(origin_frame);
    }
    if (dst != nullptr) {
      vsapi->freeFrame(dst);
    }
    vsapi->setFilterError(error.what(), frame_ctx);
    return nullptr;
  } catch (...) {
    if (origin_frame != nullptr) {
      vsapi->freeFrame(origin_frame);
    }
    if (dst != nullptr) {
      vsapi->freeFrame(dst);
    }
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

template <class Bridge>
bool accepts_video_format(VideoFormat format) {
  if constexpr (requires { Bridge::accepts_video_format(format); }) {
    return Bridge::accepts_video_format(format);
  } else {
    return true;
  }
}

inline Result<ParamValues> read_params(
  const VSMap* in,
  const FilterDescriptor& descriptor,
  const VSAPI* vsapi
);

template <VideoBridge Bridge>
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

    const auto init_result = [&]() -> Result<VideoFilterInstance<Filter>> {
      if constexpr (requires { Bridge::descriptor(); }) {
        auto params = read_params(in, Bridge::descriptor(), vsapi);
        if (!params.has_value()) {
          return Result<VideoFilterInstance<Filter>>::failure(params.error());
        }
        return init_video_filter_instance<Filter>(collected.value(), params.value());
      } else {
        return init_video_filter_instance<Filter>(collected.value());
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
  std::span<const char* const> names,
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

template <VideoBridge Bridge, class Creator>
decltype(auto) create_video_filter_bridge(Creator&& creator) {
  return std::forward<Creator>(creator).template operator()<typename Bridge::Core>(
    Bridge::vs_input_names,
    Bridge::missing_input_error,
    Bridge::vs_format_error
  );
}

} // namespace ds::vapoursynth
