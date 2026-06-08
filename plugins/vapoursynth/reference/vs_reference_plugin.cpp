#include <vapoursynth/VapourSynth4.h>

#include <dualsynth/acceptance/temporal_average3.hpp>
#include <dualsynth/reference/audio_filters.hpp>
#include <dualsynth/reference/video_filters.hpp>
#include <dualsynth/vapoursynth/video_bridge.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

struct TestPatternData {
  VSVideoInfo video_info{};
};

using VideoRequestFn = ds::Result<ds::VideoRequestResult> (*)(
  int,
  std::vector<ds::VideoFrameRequest>&
);

using VideoProcessFn = ds::Result<ds::VideoProcessResult> (*)(
  int,
  ds::VideoFrameProvider&,
  ds::MutableVideoFrameView
);

ds::VideoFormat gray8_format() {
  return ds::VideoFormat{ds::ColorFamily::Gray, ds::SampleFormat::UInt8, 1, 0, 0};
}

template <std::size_t InputCount>
struct VideoFilterData {
  std::array<VSNode*, InputCount> nodes{};
  VSVideoInfo video_info{};
  VideoRequestFn request = nullptr;
  VideoProcessFn process = nullptr;
};

struct AudioSourceData {
  VSAudioInfo audio_info{};
};

struct AudioGainData {
  VSNode* node = nullptr;
  VSAudioInfo audio_info{};
  double gain = 1.0;
};

class VSFrameProvider final : public ds::VideoFrameProvider {
public:
  VSFrameProvider(
    std::span<VSNode*> nodes,
    VSFrameContext* frame_ctx,
    const VSAPI* vsapi
  ) : nodes_(nodes),
      frame_ctx_(frame_ctx),
      vsapi_(vsapi) {}

  ~VSFrameProvider() override {
    for (const VSFrame* frame : frames_) {
      if (frame != nullptr) {
        vsapi_->freeFrame(frame);
      }
    }
  }

  ds::Result<ds::RequestedVideoFrame> get(int input_index, int frame_number) override {
    if (input_index < 0 || input_index >= static_cast<int>(nodes_.size())) {
      return ds::Result<ds::RequestedVideoFrame>::failure(
        ds::Error{ds::ErrorCode::InvalidArgument, "Video input index is out of range"}
      );
    }

    const VSFrame* frame = vsapi_->getFrameFilter(frame_number, nodes_[static_cast<std::size_t>(input_index)], frame_ctx_);
    frames_.push_back(frame);
    return ds::Result<ds::RequestedVideoFrame>::success(
      ds::RequestedVideoFrame{
        input_index,
        frame_number,
        ds::vapoursynth::make_video_frame_view(frame, gray8_format(), vsapi_)
      }
    );
  }

private:
  std::span<VSNode*> nodes_;
  VSFrameContext* frame_ctx_;
  const VSAPI* vsapi_;
  std::vector<const VSFrame*> frames_;
};

int get_required_int(const VSMap* in, const char* key, VSMap* out, const VSAPI* vsapi) {
  int error = 0;
  const int value = vsapi->mapGetIntSaturated(in, key, 0, &error);
  if (error != peSuccess) {
    vsapi->mapSetError(out, "DualSynth reference: missing required integer argument");
    return 0;
  }
  return value;
}

const VSFrame* VS_CC test_pattern_get_frame(
  int n,
  int activation_reason,
  void* instance_data,
  void**,
  VSFrameContext*,
  VSCore* core,
  const VSAPI* vsapi
) {
  if (activation_reason != arInitial && activation_reason != arAllFramesReady) {
    return nullptr;
  }

  const auto* data = static_cast<const TestPatternData*>(instance_data);
  VSFrame* frame = vsapi->newVideoFrame(
    &data->video_info.format,
    data->video_info.width,
    data->video_info.height,
    nullptr,
    core
  );

  auto* plane = vsapi->getWritePtr(frame, 0);
  const ptrdiff_t stride = vsapi->getStride(frame, 0);

  for (int y = 0; y < data->video_info.height; ++y) {
    auto* row = plane + static_cast<ptrdiff_t>(y) * stride;
    for (int x = 0; x < data->video_info.width; ++x) {
      row[x] = static_cast<std::uint8_t>((x + y + n) & 0xFF);
    }
  }

  return frame;
}

void VS_CC test_pattern_free(void* instance_data, VSCore*, const VSAPI*) {
  delete static_cast<TestPatternData*>(instance_data);
}

void VS_CC test_pattern_create(const VSMap* in, VSMap* out, void*, VSCore* core, const VSAPI* vsapi) {
  const int width = get_required_int(in, "width", out, vsapi);
  const int height = get_required_int(in, "height", out, vsapi);
  if (vsapi->mapGetError(out) != nullptr) {
    return;
  }

  if (width <= 0 || height <= 0) {
    vsapi->mapSetError(out, "DualSynth reference: width and height must be positive");
    return;
  }

  auto* data = new TestPatternData();
  if (!vsapi->queryVideoFormat(&data->video_info.format, cfGray, stInteger, 8, 0, 0, core)) {
    delete data;
    vsapi->mapSetError(out, "DualSynth reference: failed to create GRAY8 format");
    return;
  }

  data->video_info.fpsNum = 24;
  data->video_info.fpsDen = 1;
  data->video_info.width = width;
  data->video_info.height = height;
  data->video_info.numFrames = 3;

  vsapi->createVideoFilter(
    out,
    "TestPattern",
    &data->video_info,
    test_pattern_get_frame,
    test_pattern_free,
    fmParallel,
    nullptr,
    0,
    data,
    core
  );
}

template <std::size_t InputCount>
const VSFrame* VS_CC video_filter_get_frame(
  int n,
  int activation_reason,
  void* instance_data,
  void**,
  VSFrameContext* frame_ctx,
  VSCore* core,
  const VSAPI* vsapi
) {
  auto* data = static_cast<VideoFilterData<InputCount>*>(instance_data);

  if (activation_reason == arInitial) {
    std::vector<ds::VideoFrameRequest> requests;
    const auto result = data->request(n, requests);
    if (!result.has_value()) {
      return nullptr;
    }

    for (const auto& request : requests) {
      if (request.input_index < 0 || request.input_index >= static_cast<int>(data->nodes.size())) {
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

  VSFrame* dst = vsapi->newVideoFrame(
    &data->video_info.format,
    data->video_info.width,
    data->video_info.height,
    nullptr,
    core
  );

  VSFrameProvider provider(data->nodes, frame_ctx, vsapi);
  const auto result = data->process(
    n,
    provider,
    ds::vapoursynth::make_mutable_video_frame_view(dst, gray8_format(), vsapi)
  );

  if (!result.has_value()) {
    vsapi->freeFrame(dst);
    return nullptr;
  }

  return dst;
}

template <std::size_t InputCount>
void VS_CC video_filter_free(void* instance_data, VSCore*, const VSAPI* vsapi) {
  auto* data = static_cast<VideoFilterData<InputCount>*>(instance_data);
  for (VSNode* node : data->nodes) {
    if (node != nullptr) {
      vsapi->freeNode(node);
    }
  }
  delete data;
}

template <class Filter>
void create_video_filter(
  const VSMap* in,
  VSMap* out,
  VSCore* core,
  const VSAPI* vsapi,
  const std::array<const char*, static_cast<std::size_t>(Filter::input_count)>& input_names,
  const char* missing_error,
  const char* format_error
) {
  constexpr auto input_count = static_cast<std::size_t>(Filter::input_count);
  std::array<VSNode*, input_count> nodes{};
  std::array<ds::VideoInputInfo, input_count> input_infos{};

  for (std::size_t i = 0; i < input_count; ++i) {
    int error = 0;
    nodes[i] = vsapi->mapGetNode(in, input_names[i], 0, &error);
    if (error != peSuccess || nodes[i] == nullptr) {
      for (VSNode* node : nodes) {
        if (node != nullptr) {
          vsapi->freeNode(node);
        }
      }
      vsapi->mapSetError(out, missing_error);
      return;
    }

    const VSVideoInfo* input_info = vsapi->getVideoInfo(nodes[i]);
    if (input_info->format.colorFamily != cfGray ||
        input_info->format.sampleType != stInteger ||
        input_info->format.bitsPerSample != 8) {
      for (VSNode* node : nodes) {
        if (node != nullptr) {
          vsapi->freeNode(node);
        }
      }
      vsapi->mapSetError(out, format_error);
      return;
    }

    input_infos[i] = ds::VideoInputInfo{
      input_info->width,
      input_info->height,
      input_info->numFrames,
      ds::VideoFormat{ds::ColorFamily::Gray, ds::SampleFormat::UInt8, 1, 0, 0},
      ds::FrameRate{input_info->fpsNum, input_info->fpsDen}
    };
  }

  const auto collected = ds::collect_video_input_infos<Filter>(input_infos);
  if (!collected.has_value()) {
    for (VSNode* node : nodes) {
      if (node != nullptr) {
        vsapi->freeNode(node);
      }
    }
    vsapi->mapSetError(out, collected.error().message.c_str());
    return;
  }

  const auto init_result = ds::init_video_filter<Filter>(collected.value());
  if (!init_result.has_value()) {
    for (VSNode* free_node : nodes) {
      vsapi->freeNode(free_node);
    }
    vsapi->mapSetError(out, init_result.error().message.c_str());
    return;
  }

  const VSVideoInfo* base_info = vsapi->getVideoInfo(nodes[0]);
  auto* data = new VideoFilterData<input_count>();
  data->nodes = nodes;
  data->video_info = *base_info;
  data->video_info.width = init_result.value().output.width;
  data->video_info.height = init_result.value().output.height;
  data->video_info.numFrames = init_result.value().output.num_frames;
  data->video_info.fpsNum = init_result.value().output.fps.numerator;
  data->video_info.fpsDen = init_result.value().output.fps.denominator;
  data->request = ds::request_video_filter<Filter>;
  data->process = ds::process_video_filter<Filter>;

  std::array<VSFilterDependency, input_count> dependencies{};
  for (std::size_t i = 0; i < input_count; ++i) {
    dependencies[i] = VSFilterDependency{nodes[i], rpStrictSpatial};
  }

  vsapi->createVideoFilter(
    out,
    Filter::name,
    &data->video_info,
    video_filter_get_frame<input_count>,
    video_filter_free<input_count>,
    fmParallel,
    dependencies.data(),
    static_cast<int>(dependencies.size()),
    data,
    core
  );
}

struct VSVideoFilterCreator {
  const VSMap* in = nullptr;
  VSMap* out = nullptr;
  VSCore* core = nullptr;
  const VSAPI* vsapi = nullptr;

  template <class Filter>
  void operator()(
    const std::array<const char*, static_cast<std::size_t>(Filter::input_count)>& input_names,
    const char* missing_error,
    const char* format_error
  ) const {
    create_video_filter<Filter>(
      in,
      out,
      core,
      vsapi,
      input_names,
      missing_error,
      format_error
    );
  }
};

void VS_CC video_identity_create(const VSMap* in, VSMap* out, void*, VSCore* core, const VSAPI* vsapi) {
  ds::vapoursynth::create_video_filter_bridge<ds::reference::VideoIdentityBridge>(
    VSVideoFilterCreator{in, out, core, vsapi}
  );
}

void VS_CC video_invert_create(const VSMap* in, VSMap* out, void*, VSCore* core, const VSAPI* vsapi) {
  ds::vapoursynth::create_video_filter_bridge<ds::reference::VideoInvertBridge>(
    VSVideoFilterCreator{in, out, core, vsapi}
  );
}

void VS_CC video_transpose_create(const VSMap* in, VSMap* out, void*, VSCore* core, const VSAPI* vsapi) {
  ds::vapoursynth::create_video_filter_bridge<ds::reference::VideoTransposeBridge>(
    VSVideoFilterCreator{in, out, core, vsapi}
  );
}

int audio_frame_count(int64_t num_samples) {
  return static_cast<int>((num_samples + VS_AUDIO_FRAME_SAMPLES - 1) / VS_AUDIO_FRAME_SAMPLES);
}

const VSFrame* VS_CC audio_test_tone_get_frame(
  int n,
  int activation_reason,
  void* instance_data,
  void**,
  VSFrameContext*,
  VSCore* core,
  const VSAPI* vsapi
) {
  if (activation_reason != arInitial && activation_reason != arAllFramesReady) {
    return nullptr;
  }

  const auto* data = static_cast<const AudioSourceData*>(instance_data);
  const int64_t start = static_cast<int64_t>(n) * VS_AUDIO_FRAME_SAMPLES;
  const int remaining = static_cast<int>(std::max<int64_t>(0, data->audio_info.numSamples - start));
  const int sample_count = std::min(VS_AUDIO_FRAME_SAMPLES, remaining);

  VSFrame* frame = vsapi->newAudioFrame(&data->audio_info.format, sample_count, nullptr, core);
  auto* samples = reinterpret_cast<float*>(vsapi->getWritePtr(frame, 0));

  for (int i = 0; i < sample_count; ++i) {
    samples[i] = static_cast<float>(((start + i) % 256) / 255.0);
  }

  return frame;
}

void VS_CC audio_test_tone_free(void* instance_data, VSCore*, const VSAPI*) {
  delete static_cast<AudioSourceData*>(instance_data);
}

void VS_CC audio_test_tone_create(const VSMap* in, VSMap* out, void*, VSCore* core, const VSAPI* vsapi) {
  const int samples = get_required_int(in, "samples", out, vsapi);
  if (vsapi->mapGetError(out) != nullptr) {
    return;
  }

  if (samples <= 0) {
    vsapi->mapSetError(out, "DualSynth reference: samples must be positive");
    return;
  }

  auto* data = new AudioSourceData();
  const uint64_t mono_layout = 1ULL << acFrontCenter;
  if (!vsapi->queryAudioFormat(&data->audio_info.format, stFloat, 32, mono_layout, core)) {
    delete data;
    vsapi->mapSetError(out, "DualSynth reference: failed to create mono Float32 audio format");
    return;
  }

  data->audio_info.sampleRate = 48000;
  data->audio_info.numSamples = samples;
  data->audio_info.numFrames = audio_frame_count(samples);

  vsapi->createAudioFilter(
    out,
    "AudioTestTone",
    &data->audio_info,
    audio_test_tone_get_frame,
    audio_test_tone_free,
    fmParallel,
    nullptr,
    0,
    data,
    core
  );
}

const VSFrame* VS_CC audio_gain_get_frame(
  int n,
  int activation_reason,
  void* instance_data,
  void**,
  VSFrameContext* frame_ctx,
  VSCore* core,
  const VSAPI* vsapi
) {
  auto* data = static_cast<AudioGainData*>(instance_data);

  if (activation_reason == arInitial) {
    vsapi->requestFrameFilter(n, data->node, frame_ctx);
    return nullptr;
  }

  if (activation_reason != arAllFramesReady) {
    return nullptr;
  }

  const VSFrame* src = vsapi->getFrameFilter(n, data->node, frame_ctx);
  const int sample_count = vsapi->getFrameLength(src);
  VSFrame* dst = vsapi->newAudioFrame(&data->audio_info.format, sample_count, src, core);

  const auto* src_samples = reinterpret_cast<const float*>(vsapi->getReadPtr(src, 0));
  auto* dst_samples = reinterpret_cast<float*>(vsapi->getWritePtr(dst, 0));
  ds::reference::gain_samples(
    std::span<const float>(src_samples, static_cast<std::size_t>(sample_count)),
    std::span<float>(dst_samples, static_cast<std::size_t>(sample_count)),
    data->gain
  );

  vsapi->freeFrame(src);
  return dst;
}

void VS_CC audio_gain_free(void* instance_data, VSCore*, const VSAPI* vsapi) {
  auto* data = static_cast<AudioGainData*>(instance_data);
  if (data->node != nullptr) {
    vsapi->freeNode(data->node);
  }
  delete data;
}

void VS_CC audio_gain_create(const VSMap* in, VSMap* out, void*, VSCore* core, const VSAPI* vsapi) {
  int error = 0;
  VSNode* node = vsapi->mapGetNode(in, "clip", 0, &error);
  if (error != peSuccess || node == nullptr) {
    vsapi->mapSetError(out, "DualSynth reference: missing required audio clip");
    return;
  }

  const VSAudioInfo* input_info = vsapi->getAudioInfo(node);
  if (input_info->format.sampleType != stFloat ||
      input_info->format.bitsPerSample != 32 ||
      input_info->format.numChannels != 1) {
    vsapi->freeNode(node);
    vsapi->mapSetError(out, "DualSynth reference: only mono Float32 audio is supported by AudioGain");
    return;
  }

  error = 0;
  const double gain = vsapi->mapGetFloat(in, "gain", 0, &error);
  if (error != peSuccess) {
    vsapi->freeNode(node);
    vsapi->mapSetError(out, "DualSynth reference: missing required gain argument");
    return;
  }

  auto* data = new AudioGainData();
  data->node = node;
  data->audio_info = *input_info;
  data->gain = gain;

  const VSFilterDependency dependency{node, rpStrictSpatial};
  vsapi->createAudioFilter(
    out,
    "AudioGain",
    &data->audio_info,
    audio_gain_get_frame,
    audio_gain_free,
    fmParallel,
    &dependency,
    1,
    data,
    core
  );
}

void VS_CC acceptance_temporal_average3_create(
  const VSMap* in,
  VSMap* out,
  void*,
  VSCore* core,
  const VSAPI* vsapi
) {
  ds::vapoursynth::create_video_filter_bridge<ds::acceptance::AcceptanceTemporalAverage3Bridge>(
    VSVideoFilterCreator{in, out, core, vsapi}
  );
}

} // namespace

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin* plugin, const VSPLUGINAPI* vspapi) {
  vspapi->configPlugin(
    "io.luadj.dualsynth.reference",
    "dsref",
    "DualSynth Reference",
    VS_MAKE_VERSION(0, 1),
    VAPOURSYNTH_API_VERSION,
    0,
    plugin
  );

  vspapi->registerFunction(
    "TestPattern",
    "width:int;height:int;",
    "clip:vnode;",
    test_pattern_create,
    nullptr,
    plugin
  );

  vspapi->registerFunction(
    ds::reference::VideoIdentityBridge::vs_name,
    ds::reference::VideoIdentityBridge::vs_signature,
    "clip:vnode;",
    video_identity_create,
    nullptr,
    plugin
  );

  vspapi->registerFunction(
    ds::reference::VideoInvertBridge::vs_name,
    ds::reference::VideoInvertBridge::vs_signature,
    "clip:vnode;",
    video_invert_create,
    nullptr,
    plugin
  );

  vspapi->registerFunction(
    ds::reference::VideoTransposeBridge::vs_name,
    ds::reference::VideoTransposeBridge::vs_signature,
    "clip:vnode;",
    video_transpose_create,
    nullptr,
    plugin
  );

  vspapi->registerFunction(
    ds::acceptance::AcceptanceTemporalAverage3Bridge::vs_name,
    ds::acceptance::AcceptanceTemporalAverage3Bridge::vs_signature,
    "clip:vnode;",
    acceptance_temporal_average3_create,
    nullptr,
    plugin
  );

  vspapi->registerFunction(
    "AudioTestTone",
    "samples:int;",
    "clip:anode;",
    audio_test_tone_create,
    nullptr,
    plugin
  );

  vspapi->registerFunction(
    "AudioGain",
    "clip:anode;gain:float;",
    "clip:anode;",
    audio_gain_create,
    nullptr,
    plugin
  );
}
