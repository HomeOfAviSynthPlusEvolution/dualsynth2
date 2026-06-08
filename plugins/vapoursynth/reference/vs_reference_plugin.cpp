#include <vapoursynth/VapourSynth4.h>

#include <dualsynth/reference/audio_filters.hpp>
#include <dualsynth/reference/video_filters.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

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
  data->video_info.numFrames = 1;

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
