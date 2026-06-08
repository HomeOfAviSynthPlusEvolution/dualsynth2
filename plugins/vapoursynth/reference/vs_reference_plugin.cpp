#include <vapoursynth/VapourSynth4.h>

#include <dualsynth/reference/video_filters.hpp>

#include <cstddef>
#include <cstdint>

namespace {

struct TestPatternData {
  VSVideoInfo video_info{};
};

enum class VideoOperation {
  Identity,
  Invert,
};

struct VideoFilterData {
  VSNode* node = nullptr;
  VSVideoInfo video_info{};
  VideoOperation operation = VideoOperation::Identity;
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

  const int width = vsapi->getFrameWidth(src, 0);
  const int height = vsapi->getFrameHeight(src, 0);
  const ptrdiff_t src_stride = vsapi->getStride(src, 0);
  const ptrdiff_t dst_stride = vsapi->getStride(dst, 0);

  ds::PlaneSpan<const unsigned char> src_plane(
    vsapi->getReadPtr(src, 0),
    width,
    height,
    src_stride
  );
  ds::PlaneSpan<unsigned char> dst_plane(
    vsapi->getWritePtr(dst, 0),
    width,
    height,
    dst_stride
  );

  switch (data->operation) {
    case VideoOperation::Identity:
      ds::reference::copy_plane(src_plane, dst_plane);
      break;
    case VideoOperation::Invert:
      ds::reference::invert_plane(src_plane, dst_plane);
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
}
