#include <avisynth.h>

#include <dualsynth/avisynth/video_bridge.hpp>

#include "audio_filters.hpp"
#include "copy_stamp.hpp"
#include "temporal_average3.hpp"
#include "video_filters.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#if defined(_WIN32)
#define DS_AVS_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#elif defined(__clang__) || defined(__GNUC__)
#define DS_AVS_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define DS_AVS_PLUGIN_EXPORT extern "C"
#endif

const AVS_Linkage* AVS_linkage = nullptr;

namespace {

#if defined(_MSC_VER) && !defined(_M_ARM64) && !defined(__aarch64__)
void initialize_no_audio(VideoInfo& vi) {
  vi.audio_samples_per_second = 0;
  vi.sample_type = 0;
  vi.num_audio_samples = 0;
  vi.nchannels = 0;
}

void initialize_no_video(VideoInfo& vi) {
  vi.width = 0;
  vi.height = 0;
  vi.fps_numerator = 0;
  vi.fps_denominator = 1;
  vi.num_frames = 0;
  vi.pixel_type = VideoInfo::CS_UNKNOWN;
}
#endif

#if defined(_MSC_VER) && !defined(_M_ARM64) && !defined(__aarch64__)
class TestPatternClip final : public IClip {
public:
  TestPatternClip(int width, int height) {
    vi_.width = width;
    vi_.height = height;
    vi_.pixel_type = VideoInfo::CS_Y8;
    vi_.fps_numerator = 24;
    vi_.fps_denominator = 1;
    vi_.num_frames = 3;
    vi_.image_type = 0;
    initialize_no_audio(vi_);
  }

  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override {
    PVideoFrame frame = env->NewVideoFrame(vi_);
    auto* dst = frame->GetWritePtr(PLANAR_Y);
    const int stride = frame->GetPitch(PLANAR_Y);

    for (int y = 0; y < vi_.height; ++y) {
      auto* row = dst + static_cast<std::ptrdiff_t>(y) * stride;
      for (int x = 0; x < vi_.width; ++x) {
        row[x] = static_cast<BYTE>((x + y + n) & 0xFF);
      }
    }

    return frame;
  }

  bool __stdcall GetParity(int) override {
    return false;
  }

  void __stdcall GetAudio(void*, int64_t, int64_t, IScriptEnvironment*) override {}

  int __stdcall SetCacheHints(int cachehints, int frame_range) override {
    return ds::avisynth::cache_hint_response(
      cachehints,
      frame_range,
      ds::avisynth::MtMode::NiceFilter
    );
  }

  const VideoInfo& __stdcall GetVideoInfo() override {
    return vi_;
  }

private:
  VideoInfo vi_{};
};

class AudioTestToneClip final : public IClip {
public:
  explicit AudioTestToneClip(int samples) {
    initialize_no_video(vi_);
    vi_.audio_samples_per_second = 48000;
    vi_.sample_type = SAMPLE_FLOAT;
    vi_.num_audio_samples = samples;
    vi_.nchannels = 1;
    vi_.image_type = 0;
  }

  PVideoFrame __stdcall GetFrame(int, IScriptEnvironment* env) override {
    env->ThrowError("DualSynth reference: DSAudioTestTone has no video");
    return {};
  }

  bool __stdcall GetParity(int) override {
    return false;
  }

  void __stdcall GetAudio(void* buf, int64_t start, int64_t count, IScriptEnvironment*) override {
    auto* samples = static_cast<float*>(buf);
    for (int64_t i = 0; i < count; ++i) {
      samples[i] = static_cast<float>(((start + i) % 256) / 255.0);
    }
  }

  int __stdcall SetCacheHints(int cachehints, int frame_range) override {
    return ds::avisynth::cache_hint_response(
      cachehints,
      frame_range,
      ds::avisynth::MtMode::NiceFilter
    );
  }

  const VideoInfo& __stdcall GetVideoInfo() override {
    return vi_;
  }

private:
  VideoInfo vi_{};
};

class AudioGainFilter final : public GenericVideoFilter {
public:
  AudioGainFilter(PClip child, double gain)
    : GenericVideoFilter(child),
      gain_(gain) {}

  void __stdcall GetAudio(void* buf, int64_t start, int64_t count, IScriptEnvironment* env) override {
    const auto sample_count = static_cast<std::size_t>(count) * static_cast<std::size_t>(vi.AudioChannels());
    std::vector<float> input(sample_count);
    child->GetAudio(input.data(), start, count, env);

    auto* output = static_cast<float*>(buf);
    ds::reference::gain_samples(
      ds::Span<const float>(input.data(), input.size()),
      ds::Span<float>(output, sample_count),
      gain_
    );
  }

  int __stdcall SetCacheHints(int cachehints, int frame_range) override {
    return ds::avisynth::cache_hint_response(
      cachehints,
      frame_range,
      ds::avisynth::MtMode::NiceFilter
    );
  }

private:
  double gain_;
};

AVSValue __cdecl create_test_pattern(AVSValue args, void*, IScriptEnvironment* env) {
  const int width = args[0].AsInt();
  const int height = args[1].AsInt();

  if (width <= 0 || height <= 0) {
    env->ThrowError("DualSynth reference: width and height must be positive");
  }

  return new TestPatternClip(width, height);
}

AVSValue __cdecl create_video_identity(AVSValue args, void*, IScriptEnvironment* env) {
  return ds::avisynth::create_video_filter_bridge<ds::reference::VideoIdentityBridge>(args, env);
}

AVSValue __cdecl create_video_invert(AVSValue args, void*, IScriptEnvironment* env) {
  return ds::avisynth::create_video_filter_bridge<ds::reference::VideoInvertBridge>(args, env);
}

AVSValue __cdecl create_video_transpose(AVSValue args, void*, IScriptEnvironment* env) {
  return ds::avisynth::create_video_filter_bridge<ds::reference::VideoTransposeBridge>(args, env);
}

AVSValue __cdecl create_audio_test_tone(AVSValue args, void*, IScriptEnvironment* env) {
  const int samples = args[0].AsInt();
  if (samples <= 0) {
    env->ThrowError("DualSynth reference: samples must be positive");
  }

  return new AudioTestToneClip(samples);
}

AVSValue __cdecl create_audio_gain(AVSValue args, void*, IScriptEnvironment* env) {
  PClip clip = args[0].AsClip();
  const VideoInfo& vi = clip->GetVideoInfo();
  if (!vi.HasAudio() || !vi.IsSampleType(SAMPLE_FLOAT) || vi.AudioChannels() != 1) {
    env->ThrowError("DualSynth reference: DSAudioGain supports only mono Float32 audio");
  }

  return new AudioGainFilter(clip, args[1].AsFloat());
}

AVSValue __cdecl create_acceptance_temporal_average3(AVSValue args, void*, IScriptEnvironment* env) {
  return ds::avisynth::create_video_filter_bridge<ds::acceptance::AcceptanceTemporalAverage3Bridge>(args, env);
}

AVSValue __cdecl create_acceptance_copy_stamp(AVSValue args, void*, IScriptEnvironment* env) {
  return ds::avisynth::create_video_filter_bridge<ds::acceptance::AcceptanceCopyStampBridge>(args, env);
}
#endif

struct CTestPatternData {
  int width;
  int height;
};

AVS_VideoFrame* AVSC_CC c_test_pattern_get_frame(AVS_FilterInfo* fi, int n) {
  auto* data = static_cast<CTestPatternData*>(fi->user_data);
  AVS_VideoFrame* frame = ds::avisynth::c::new_video_frame(fi->env, &fi->vi);
  auto* dst = ds::avisynth::c::get_write_ptr_p(frame, AVS_PLANAR_Y);
  const int stride = ds::avisynth::c::get_pitch_p(frame, AVS_PLANAR_Y);

  for (int y = 0; y < data->height; ++y) {
    auto* row = dst + static_cast<std::ptrdiff_t>(y) * stride;
    for (int x = 0; x < data->width; ++x) {
      row[x] = static_cast<BYTE>((x + y + n) & 0xFF);
    }
  }

  return frame;
}

int AVSC_CC c_test_pattern_set_cache_hints(AVS_FilterInfo*, int cachehints, int) {
  if (cachehints == AVS_CACHE_GET_MTMODE) {
    return AVS_MT_NICE_FILTER;
  }
  return 0;
}

void AVSC_CC c_test_pattern_free(AVS_FilterInfo* fi) {
  delete static_cast<CTestPatternData*>(fi->user_data);
  fi->user_data = nullptr;
}

AVS_Value AVSC_CC c_create_test_pattern(AVS_ScriptEnvironment* env, AVS_Value args, void*) {
  const int width = avs_as_int(avs_array_elt(args, 0));
  const int height = avs_as_int(avs_array_elt(args, 1));
  if (width <= 0 || height <= 0) {
    return ds::avisynth::c::avs_new_value_error("DualSynth reference: width and height must be positive");
  }

  AVS_FilterInfo* fi = nullptr;
  AVS_Clip* clip = ds::avisynth::c::new_c_filter(env, &fi, avs_void, 0);
  if (!clip || !fi) {
    return ds::avisynth::c::avs_new_value_error("DualSynth reference: failed to create test pattern filter");
  }

  fi->vi.width = width;
  fi->vi.height = height;
  fi->vi.pixel_type = AVS_CS_Y8;
  fi->vi.fps_numerator = 24;
  fi->vi.fps_denominator = 1;
  fi->vi.num_frames = 3;
  fi->vi.image_type = 0;
  ds::avisynth::c::initialize_no_audio(fi->vi);

  fi->user_data = new CTestPatternData{width, height};
  fi->get_frame = c_test_pattern_get_frame;
  fi->set_cache_hints = c_test_pattern_set_cache_hints;
  fi->free_filter = c_test_pattern_free;

  AVS_Value result;
  ds::avisynth::c::set_to_clip(&result, clip);
  ds::avisynth::c::release_clip(clip);
  return result;
}

struct CAudioToneData {
  int samples;
};

AVS_VideoFrame* AVSC_CC c_audio_tone_get_frame(AVS_FilterInfo* fi, int) {
  fi->error = "DualSynth reference: DSAudioTestTone has no video";
  return nullptr;
}

int AVSC_CC c_audio_tone_get_audio(AVS_FilterInfo*, void* buf, int64_t start, int64_t count) {
  auto* samples = static_cast<float*>(buf);
  for (int64_t i = 0; i < count; ++i) {
    samples[i] = static_cast<float>(((start + i) % 256) / 255.0);
  }
  return 0;
}

int AVSC_CC c_audio_tone_set_cache_hints(AVS_FilterInfo*, int cachehints, int) {
  if (cachehints == AVS_CACHE_GET_MTMODE) {
    return AVS_MT_NICE_FILTER;
  }
  return 0;
}

void AVSC_CC c_audio_tone_free(AVS_FilterInfo* fi) {
  delete static_cast<CAudioToneData*>(fi->user_data);
  fi->user_data = nullptr;
}

AVS_Value AVSC_CC c_create_audio_test_tone(AVS_ScriptEnvironment* env, AVS_Value args, void*) {
  const int samples = avs_as_int(avs_array_elt(args, 0));
  if (samples <= 0) {
    return ds::avisynth::c::avs_new_value_error("DualSynth reference: samples must be positive");
  }

  AVS_FilterInfo* fi = nullptr;
  AVS_Clip* clip = ds::avisynth::c::new_c_filter(env, &fi, avs_void, 0);
  if (!clip || !fi) {
    return ds::avisynth::c::avs_new_value_error("DualSynth reference: failed to create audio test tone filter");
  }

  ds::avisynth::c::initialize_no_video(fi->vi);
  fi->vi.audio_samples_per_second = 48000;
  fi->vi.sample_type = AVS_SAMPLE_FLOAT;
  fi->vi.num_audio_samples = samples;
  fi->vi.nchannels = 1;
  fi->vi.image_type = 0;

  fi->user_data = new CAudioToneData{samples};
  fi->get_frame = c_audio_tone_get_frame;
  fi->get_audio = c_audio_tone_get_audio;
  fi->set_cache_hints = c_audio_tone_set_cache_hints;
  fi->free_filter = c_audio_tone_free;

  AVS_Value result;
  ds::avisynth::c::set_to_clip(&result, clip);
  ds::avisynth::c::release_clip(clip);
  return result;
}

struct CAudioGainData {
  AVS_Clip* child;
  double gain;
};

int AVSC_CC c_audio_gain_get_audio(AVS_FilterInfo* fi, void* buf, int64_t start, int64_t count) {
  auto* data = static_cast<CAudioGainData*>(fi->user_data);
  const auto sample_count = static_cast<std::size_t>(count) * static_cast<std::size_t>(fi->vi.nchannels);
  std::vector<float> input(sample_count);
  ds::avisynth::c::get_audio(data->child, input.data(), start, count);

  auto* output = static_cast<float*>(buf);
  ds::reference::gain_samples(
    ds::Span<const float>(input.data(), input.size()),
    ds::Span<float>(output, sample_count),
    data->gain
  );
  return 0;
}

int AVSC_CC c_audio_gain_set_cache_hints(AVS_FilterInfo*, int cachehints, int) {
  if (cachehints == AVS_CACHE_GET_MTMODE) {
    return AVS_MT_NICE_FILTER;
  }
  return 0;
}

void AVSC_CC c_audio_gain_free(AVS_FilterInfo* fi) {
  auto* data = static_cast<CAudioGainData*>(fi->user_data);
  if (data) {
    if (data->child) {
      ds::avisynth::c::release_clip(data->child);
    }
    delete data;
  }
  fi->user_data = nullptr;
}

AVS_Value AVSC_CC c_create_audio_gain(AVS_ScriptEnvironment* env, AVS_Value args, void*) {
  AVS_Value clip_val = avs_array_elt(args, 0);
  if (!avs_is_clip(clip_val)) {
    return ds::avisynth::c::avs_new_value_error("DualSynth reference: DSAudioGain missing input clip");
  }
  AVS_Clip* child = ds::avisynth::c::take_clip(clip_val, env);
  const AVS_VideoInfo* vi = ds::avisynth::c::get_video_info(child);
  if (!vi || !avs_has_audio(vi) || vi->sample_type != AVS_SAMPLE_FLOAT || vi->nchannels != 1) {
    ds::avisynth::c::release_clip(child);
    return ds::avisynth::c::avs_new_value_error("DualSynth reference: DSAudioGain supports only mono Float32 audio");
  }

  const double gain = avs_as_float(avs_array_elt(args, 1));

  AVS_FilterInfo* fi = nullptr;
  AVS_Clip* clip = ds::avisynth::c::new_c_filter(env, &fi, avs_void, 0);
  if (!clip || !fi) {
    ds::avisynth::c::release_clip(child);
    return ds::avisynth::c::avs_new_value_error("DualSynth reference: failed to create audio gain filter");
  }

  fi->vi = *vi;
  fi->user_data = new CAudioGainData{child, gain};
  fi->get_audio = c_audio_gain_get_audio;
  fi->set_cache_hints = c_audio_gain_set_cache_hints;
  fi->free_filter = c_audio_gain_free;

  AVS_Value result;
  ds::avisynth::c::set_to_clip(&result, clip);
  ds::avisynth::c::release_clip(clip);
  return result;
}

} // namespace

#if defined(_MSC_VER) && !defined(_M_ARM64) && !defined(__aarch64__)
DS_AVS_PLUGIN_EXPORT const char* __stdcall AvisynthPluginInit3(
  IScriptEnvironment* env,
  const AVS_Linkage* const vectors
) {
  AVS_linkage = vectors;
  env->AddFunction("DSTestPattern", "ii", create_test_pattern, nullptr);
  ds::avisynth::set_filter_mt_mode(env, "DSTestPattern", ds::avisynth::MtMode::NiceFilter);
  env->AddFunction(
    ds::reference::VideoIdentityBridge::avs_name,
    ds::reference::VideoIdentityBridge::avs_signature,
    create_video_identity,
    nullptr
  );
  ds::avisynth::set_video_filter_mt_mode<ds::reference::VideoIdentityBridge>(env);
  env->AddFunction(
    ds::reference::VideoInvertBridge::avs_name,
    ds::reference::VideoInvertBridge::avs_signature,
    create_video_invert,
    nullptr
  );
  ds::avisynth::set_video_filter_mt_mode<ds::reference::VideoInvertBridge>(env);
  env->AddFunction(
    ds::reference::VideoTransposeBridge::avs_name,
    ds::reference::VideoTransposeBridge::avs_signature,
    create_video_transpose,
    nullptr
  );
  ds::avisynth::set_video_filter_mt_mode<ds::reference::VideoTransposeBridge>(env);
  env->AddFunction(
    ds::acceptance::AcceptanceTemporalAverage3Bridge::avs_name,
    ds::acceptance::AcceptanceTemporalAverage3Bridge::avs_signature,
    create_acceptance_temporal_average3,
    nullptr
  );
  ds::avisynth::set_video_filter_mt_mode<ds::acceptance::AcceptanceTemporalAverage3Bridge>(env);
  env->AddFunction(
    ds::acceptance::AcceptanceCopyStampBridge::avs_name,
    ds::acceptance::AcceptanceCopyStampBridge::avs_signature,
    create_acceptance_copy_stamp,
    nullptr
  );
  ds::avisynth::set_video_filter_mt_mode<ds::acceptance::AcceptanceCopyStampBridge>(env);
  env->AddFunction("DSAudioTestTone", "i", create_audio_test_tone, nullptr);
  ds::avisynth::set_filter_mt_mode(env, "DSAudioTestTone", ds::avisynth::MtMode::NiceFilter);
  env->AddFunction("DSAudioGain", "cf", create_audio_gain, nullptr);
  ds::avisynth::set_filter_mt_mode(env, "DSAudioGain", ds::avisynth::MtMode::NiceFilter);
  return "DualSynth reference plugin";
}
#endif

DS_AVS_PLUGIN_EXPORT const char* AVSC_CC avisynth_c_plugin_init2(
  AVS_ScriptEnvironment* env
) {
  ds::avisynth::c::add_function(env, "DSTestPattern", "ii", c_create_test_pattern, nullptr);
  ds::avisynth::c::register_video_filter<ds::reference::VideoIdentityBridge>(env);
  ds::avisynth::c::register_video_filter<ds::reference::VideoInvertBridge>(env);
  ds::avisynth::c::register_video_filter<ds::reference::VideoTransposeBridge>(env);
  ds::avisynth::c::register_video_filter<ds::acceptance::AcceptanceTemporalAverage3Bridge>(env);
  ds::avisynth::c::register_video_filter<ds::acceptance::AcceptanceCopyStampBridge>(env);
  ds::avisynth::c::add_function(env, "DSAudioTestTone", "i", c_create_audio_test_tone, nullptr);
  ds::avisynth::c::add_function(env, "DSAudioGain", "cf", c_create_audio_gain, nullptr);
  return "DualSynth reference plugin";
}
