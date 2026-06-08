#include <vapoursynth/VapourSynth4.h>

#include <dualsynth/acceptance/temporal_average3.hpp>
#include <dualsynth/reference/audio_filters.hpp>
#include <dualsynth/reference/video_filters.hpp>

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

enum class VideoOperation {
  Identity,
  Invert,
  Transpose,
};

struct VideoFilterData {
  VSNode* node = nullptr;
  VSVideoInfo video_info{};
  VideoOperation operation = VideoOperation::Identity;
};

struct AudioSourceData {
  VSAudioInfo audio_info{};
};

struct AudioGainData {
  VSNode* node = nullptr;
  VSAudioInfo audio_info{};
  double gain = 1.0;
};

struct AcceptanceTemporalAverage3Data {
  std::array<VSNode*, 3> nodes{};
  VSVideoInfo video_info{};
};

class VSFrameProvider final : public ds::VideoFrameProvider {
public:
  VSFrameProvider(
    const std::array<VSNode*, 3>& nodes,
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
        ds::Error{ds::ErrorCode::InvalidArgument, "AcceptanceTemporalAverage3 input index is out of range"}
      );
    }

    const VSFrame* frame = vsapi_->getFrameFilter(frame_number, nodes_[static_cast<std::size_t>(input_index)], frame_ctx_);
    frames_.push_back(frame);
    return ds::Result<ds::RequestedVideoFrame>::success(
      ds::RequestedVideoFrame{
        input_index,
        frame_number,
        ds::PlaneSpan<const unsigned char>(
          vsapi_->getReadPtr(frame, 0),
          vsapi_->getFrameWidth(frame, 0),
          vsapi_->getFrameHeight(frame, 0),
          vsapi_->getStride(frame, 0)
        )
      }
    );
  }

private:
  std::array<VSNode*, 3> nodes_;
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

const VSFrame* VS_CC video_filter_get_frame(
  int n,
  int activation_reason,
  void* instance_data,
  void**,
  VSFrameContext* frame_ctx,
  VSCore* core,
  const VSAPI* vsapi
) {
  auto* data = static_cast<VideoFilterData*>(instance_data);

  if (activation_reason == arInitial) {
    vsapi->requestFrameFilter(n, data->node, frame_ctx);
    return nullptr;
  }

  if (activation_reason != arAllFramesReady) {
    return nullptr;
  }

  const VSFrame* src = vsapi->getFrameFilter(n, data->node, frame_ctx);
  VSFrame* dst = vsapi->newVideoFrame(
    &data->video_info.format,
    data->video_info.width,
    data->video_info.height,
    src,
    core
  );

  const int src_width = vsapi->getFrameWidth(src, 0);
  const int src_height = vsapi->getFrameHeight(src, 0);
  const int dst_width = vsapi->getFrameWidth(dst, 0);
  const int dst_height = vsapi->getFrameHeight(dst, 0);
  const ptrdiff_t src_stride = vsapi->getStride(src, 0);
  const ptrdiff_t dst_stride = vsapi->getStride(dst, 0);

  ds::PlaneSpan<const unsigned char> src_plane(
    vsapi->getReadPtr(src, 0),
    src_width,
    src_height,
    src_stride
  );
  ds::PlaneSpan<unsigned char> dst_plane(
    vsapi->getWritePtr(dst, 0),
    dst_width,
    dst_height,
    dst_stride
  );

  switch (data->operation) {
    case VideoOperation::Identity:
      ds::reference::copy_plane(src_plane, dst_plane);
      break;
    case VideoOperation::Invert:
      ds::reference::invert_plane(src_plane, dst_plane);
      break;
    case VideoOperation::Transpose:
      ds::reference::transpose_plane(src_plane, dst_plane);
      break;
  }

  vsapi->freeFrame(src);
  return dst;
}

void VS_CC video_filter_free(void* instance_data, VSCore*, const VSAPI* vsapi) {
  auto* data = static_cast<VideoFilterData*>(instance_data);
  if (data->node != nullptr) {
    vsapi->freeNode(data->node);
  }
  delete data;
}

void create_video_filter(
  const VSMap* in,
  VSMap* out,
  VSCore* core,
  const VSAPI* vsapi,
  VideoOperation operation,
  const char* name
) {
  int error = 0;
  VSNode* node = vsapi->mapGetNode(in, "clip", 0, &error);
  if (error != peSuccess || node == nullptr) {
    vsapi->mapSetError(out, "DualSynth reference: missing required video clip");
    return;
  }

  const VSVideoInfo* input_info = vsapi->getVideoInfo(node);
  if (input_info->format.colorFamily != cfGray ||
      input_info->format.sampleType != stInteger ||
      input_info->format.bitsPerSample != 8) {
    vsapi->freeNode(node);
    vsapi->mapSetError(out, "DualSynth reference: only GRAY8 is supported by this VS reference filter");
    return;
  }

  auto* data = new VideoFilterData();
  data->node = node;
  data->video_info = *input_info;
  data->operation = operation;
  if (operation == VideoOperation::Transpose) {
    const int width = data->video_info.width;
    data->video_info.width = data->video_info.height;
    data->video_info.height = width;
  }

  const VSFilterDependency dependency{node, rpStrictSpatial};
  vsapi->createVideoFilter(
    out,
    name,
    &data->video_info,
    video_filter_get_frame,
    video_filter_free,
    fmParallel,
    &dependency,
    1,
    data,
    core
  );
}

void VS_CC video_identity_create(const VSMap* in, VSMap* out, void*, VSCore* core, const VSAPI* vsapi) {
  create_video_filter(in, out, core, vsapi, VideoOperation::Identity, "VideoIdentity");
}

void VS_CC video_invert_create(const VSMap* in, VSMap* out, void*, VSCore* core, const VSAPI* vsapi) {
  create_video_filter(in, out, core, vsapi, VideoOperation::Invert, "VideoInvert");
}

void VS_CC video_transpose_create(const VSMap* in, VSMap* out, void*, VSCore* core, const VSAPI* vsapi) {
  create_video_filter(in, out, core, vsapi, VideoOperation::Transpose, "VideoTranspose");
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

const VSFrame* VS_CC acceptance_temporal_average3_get_frame(
  int n,
  int activation_reason,
  void* instance_data,
  void**,
  VSFrameContext* frame_ctx,
  VSCore* core,
  const VSAPI* vsapi
) {
  auto* data = static_cast<AcceptanceTemporalAverage3Data*>(instance_data);

  if (activation_reason == arInitial) {
    vsapi->requestFrameFilter(n - 1, data->nodes[0], frame_ctx);
    vsapi->requestFrameFilter(n, data->nodes[1], frame_ctx);
    vsapi->requestFrameFilter(n + 1, data->nodes[2], frame_ctx);
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
  ds::VideoProcessContext context{
    n,
    provider,
    ds::PlaneSpan<unsigned char>(
      vsapi->getWritePtr(dst, 0),
      data->video_info.width,
      data->video_info.height,
      vsapi->getStride(dst, 0)
    )
  };

  const auto result = ds::acceptance::temporal_average3_process(context);
  if (!result.has_value()) {
    vsapi->freeFrame(dst);
    return nullptr;
  }

  return dst;
}

void VS_CC acceptance_temporal_average3_free(void* instance_data, VSCore*, const VSAPI* vsapi) {
  auto* data = static_cast<AcceptanceTemporalAverage3Data*>(instance_data);
  for (VSNode* node : data->nodes) {
    if (node != nullptr) {
      vsapi->freeNode(node);
    }
  }
  delete data;
}

void VS_CC acceptance_temporal_average3_create(
  const VSMap* in,
  VSMap* out,
  void*,
  VSCore* core,
  const VSAPI* vsapi
) {
  std::array<const char*, 3> names{"a", "b", "c"};
  std::array<VSNode*, 3> nodes{};

  for (std::size_t i = 0; i < names.size(); ++i) {
    int error = 0;
    nodes[i] = vsapi->mapGetNode(in, names[i], 0, &error);
    if (error != peSuccess || nodes[i] == nullptr) {
      for (VSNode* node : nodes) {
        if (node != nullptr) {
          vsapi->freeNode(node);
        }
      }
      vsapi->mapSetError(out, "DualSynth reference: missing required AcceptanceTemporalAverage3 clip");
      return;
    }
  }

  const VSVideoInfo* info = vsapi->getVideoInfo(nodes[1]);
  if (info->format.colorFamily != cfGray ||
      info->format.sampleType != stInteger ||
      info->format.bitsPerSample != 8) {
    for (VSNode* node : nodes) {
      vsapi->freeNode(node);
    }
    vsapi->mapSetError(out, "DualSynth reference: AcceptanceTemporalAverage3 supports only GRAY8 video");
    return;
  }

  for (VSNode* node : nodes) {
    const VSVideoInfo* input_info = vsapi->getVideoInfo(node);
    if (input_info->width != info->width ||
        input_info->height != info->height ||
        input_info->numFrames != info->numFrames ||
        input_info->format.colorFamily != info->format.colorFamily ||
        input_info->format.sampleType != info->format.sampleType ||
        input_info->format.bitsPerSample != info->format.bitsPerSample) {
      for (VSNode* free_node : nodes) {
        vsapi->freeNode(free_node);
      }
      vsapi->mapSetError(out, "DualSynth reference: AcceptanceTemporalAverage3 inputs must have matching GRAY8 video info");
      return;
    }
  }

  auto* data = new AcceptanceTemporalAverage3Data();
  data->nodes = nodes;
  data->video_info = *info;

  std::array<VSFilterDependency, 3> dependencies{
    VSFilterDependency{nodes[0], rpStrictSpatial},
    VSFilterDependency{nodes[1], rpStrictSpatial},
    VSFilterDependency{nodes[2], rpStrictSpatial}
  };

  vsapi->createVideoFilter(
    out,
    "AcceptanceTemporalAverage3",
    &data->video_info,
    acceptance_temporal_average3_get_frame,
    acceptance_temporal_average3_free,
    fmParallel,
    dependencies.data(),
    static_cast<int>(dependencies.size()),
    data,
    core
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
    "VideoIdentity",
    "clip:vnode;",
    "clip:vnode;",
    video_identity_create,
    nullptr,
    plugin
  );

  vspapi->registerFunction(
    "VideoInvert",
    "clip:vnode;",
    "clip:vnode;",
    video_invert_create,
    nullptr,
    plugin
  );

  vspapi->registerFunction(
    "VideoTranspose",
    "clip:vnode;",
    "clip:vnode;",
    video_transpose_create,
    nullptr,
    plugin
  );

  vspapi->registerFunction(
    "AcceptanceTemporalAverage3",
    "a:vnode;b:vnode;c:vnode;",
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
