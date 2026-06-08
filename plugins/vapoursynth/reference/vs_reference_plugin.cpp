#include <vapoursynth/VapourSynth4.h>

#include <cstdint>

namespace {

struct TestPatternData {
  VSVideoInfo video_info{};
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
  if (activation_reason != arAllFramesReady) {
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
}
