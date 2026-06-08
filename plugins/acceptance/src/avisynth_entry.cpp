#include <avisynth.h>

#include <dualsynth/avisynth/video_bridge.hpp>

#include "audio_filters.hpp"
#include "temporal_average3.hpp"
#include "video_filters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
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

ds::VideoFormat gray8_format() {
  return ds::VideoFormat{ds::ColorFamily::Gray, ds::SampleFormat::UInt8, 1, 0, 0};
}

ds::VideoFrameView make_const_video_frame_view(const PVideoFrame& frame, const VideoInfo& vi) {
  (void)vi;
  return ds::avisynth::make_video_frame_view(frame, gray8_format());
}

class AVSFrameProvider final : public ds::VideoFrameProvider {
public:
  AVSFrameProvider(std::span<PClip> clips, IScriptEnvironment* env)
    : clips_(clips),
      env_(env) {}

  ds::Result<ds::RequestedVideoFrame> get(int input_index, int frame_number) override {
    if (input_index < 0 || input_index >= static_cast<int>(clips_.size())) {
      return ds::Result<ds::RequestedVideoFrame>::failure(
        ds::Error{ds::ErrorCode::InvalidArgument, "Video input index is out of range"}
      );
    }

    PVideoFrame frame = clips_[static_cast<std::size_t>(input_index)]->GetFrame(frame_number, env_);
    const VideoInfo& vi = clips_[static_cast<std::size_t>(input_index)]->GetVideoInfo();
    frames_.push_back(frame);
    return ds::Result<ds::RequestedVideoFrame>::success(
      ds::RequestedVideoFrame{
        input_index,
        frame_number,
        make_const_video_frame_view(frame, vi)
      }
    );
  }

private:
  std::span<PClip> clips_;
  IScriptEnvironment* env_;
  std::vector<PVideoFrame> frames_;
};

using VideoProcessFn = ds::Result<ds::VideoProcessResult> (*)(
  int,
  ds::VideoFrameProvider&,
  ds::MutableVideoFrameView
);

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

  int __stdcall SetCacheHints(int, int) override {
    return 0;
  }

  const VideoInfo& __stdcall GetVideoInfo() override {
    return vi_;
  }

private:
  VideoInfo vi_{};
};

template <std::size_t InputCount>
class VideoFilter final : public IClip {
public:
  VideoFilter(
    std::array<PClip, InputCount> clips,
    ds::VideoOutputInfo output,
    VideoProcessFn process,
    std::size_t parity_source_index,
    bool forward_audio
  ) : clips_(clips),
      process_(process),
      parity_source_index_(parity_source_index),
      forward_audio_(forward_audio) {
    vi_ = clips_[0]->GetVideoInfo();
    vi_.width = output.width;
    vi_.height = output.height;
    vi_.num_frames = output.num_frames;
    vi_.fps_numerator = static_cast<unsigned>(output.fps.numerator);
    vi_.fps_denominator = static_cast<unsigned>(output.fps.denominator);
    if (!forward_audio_) {
      initialize_no_audio(vi_);
    }
  }

  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override {
    PVideoFrame dst = env->NewVideoFrame(vi_);
    AVSFrameProvider provider(clips_, env);
    const auto result = process_(
      n,
      provider,
      ds::avisynth::make_mutable_video_frame_view(dst, gray8_format())
    );

    if (!result.has_value()) {
      env->ThrowError(result.error().message.c_str());
    }

    return dst;
  }

  bool __stdcall GetParity(int n) override {
    return clips_[parity_source_index_]->GetParity(n);
  }

  void __stdcall GetAudio(void* buf, int64_t start, int64_t count, IScriptEnvironment* env) override {
    if (!forward_audio_) {
      env->ThrowError("DualSynth reference: video filter has no audio");
    }
    clips_[0]->GetAudio(buf, start, count, env);
  }

  int __stdcall SetCacheHints(int, int) override {
    return 0;
  }

  const VideoInfo& __stdcall GetVideoInfo() override {
    return vi_;
  }

private:
  std::array<PClip, InputCount> clips_;
  VideoProcessFn process_;
  std::size_t parity_source_index_;
  bool forward_audio_;
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

  int __stdcall SetCacheHints(int, int) override {
    return 0;
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
      std::span<const float>(input.data(), input.size()),
      std::span<float>(output, sample_count),
      gain_
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

template <class Filter>
AVSValue create_video_filter(
  AVSValue args,
  IScriptEnvironment* env,
  const char* format_error,
  std::size_t parity_source_index = 0,
  bool forward_audio = true
) {
  constexpr auto input_count = static_cast<std::size_t>(Filter::input_count);
  std::array<PClip, input_count> clips{};
  std::array<ds::VideoInputInfo, input_count> input_infos{};

  for (std::size_t i = 0; i < input_count; ++i) {
    clips[i] = args[static_cast<int>(i)].AsClip();
    const VideoInfo& vi = clips[i]->GetVideoInfo();
    if (!vi.HasVideo() || !vi.IsColorSpace(VideoInfo::CS_Y8)) {
      env->ThrowError(format_error);
    }
    input_infos[i] = ds::VideoInputInfo{
      vi.width,
      vi.height,
      vi.num_frames,
      ds::VideoFormat{ds::ColorFamily::Gray, ds::SampleFormat::UInt8, 1, 0, 0},
      ds::FrameRate{vi.fps_numerator, vi.fps_denominator}
    };
  }

  const auto collected = ds::collect_video_input_infos<Filter>(input_infos);
  if (!collected.has_value()) {
    env->ThrowError(collected.error().message.c_str());
  }

  const auto init_result = ds::init_video_filter<Filter>(collected.value());
  if (!init_result.has_value()) {
    env->ThrowError(init_result.error().message.c_str());
  }

  return new VideoFilter<input_count>(
    clips,
    init_result.value().output,
    ds::process_video_filter<Filter>,
    parity_source_index,
    forward_audio
  );
}

template <class Bridge>
AVSValue create_video_filter_bridge(AVSValue args, IScriptEnvironment* env) {
  return create_video_filter<typename Bridge::Core>(
    args,
    env,
    Bridge::avs_format_error,
    Bridge::parity_source_index,
    Bridge::forward_audio
  );
}

AVSValue __cdecl create_video_identity(AVSValue args, void*, IScriptEnvironment* env) {
  return create_video_filter_bridge<ds::reference::VideoIdentityBridge>(args, env);
}

AVSValue __cdecl create_video_invert(AVSValue args, void*, IScriptEnvironment* env) {
  return create_video_filter_bridge<ds::reference::VideoInvertBridge>(args, env);
}

AVSValue __cdecl create_video_transpose(AVSValue args, void*, IScriptEnvironment* env) {
  return create_video_filter_bridge<ds::reference::VideoTransposeBridge>(args, env);
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
  return create_video_filter_bridge<ds::acceptance::AcceptanceTemporalAverage3Bridge>(args, env);
}

} // namespace

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
  ds::avisynth::set_filter_mt_mode(
    env,
    ds::reference::VideoIdentityBridge::avs_name,
    ds::avisynth::MtMode::NiceFilter
  );
  env->AddFunction(
    ds::reference::VideoInvertBridge::avs_name,
    ds::reference::VideoInvertBridge::avs_signature,
    create_video_invert,
    nullptr
  );
  ds::avisynth::set_filter_mt_mode(
    env,
    ds::reference::VideoInvertBridge::avs_name,
    ds::avisynth::MtMode::NiceFilter
  );
  env->AddFunction(
    ds::reference::VideoTransposeBridge::avs_name,
    ds::reference::VideoTransposeBridge::avs_signature,
    create_video_transpose,
    nullptr
  );
  ds::avisynth::set_filter_mt_mode(
    env,
    ds::reference::VideoTransposeBridge::avs_name,
    ds::avisynth::MtMode::NiceFilter
  );
  env->AddFunction(
    ds::acceptance::AcceptanceTemporalAverage3Bridge::avs_name,
    ds::acceptance::AcceptanceTemporalAverage3Bridge::avs_signature,
    create_acceptance_temporal_average3,
    nullptr
  );
  ds::avisynth::set_filter_mt_mode(
    env,
    ds::acceptance::AcceptanceTemporalAverage3Bridge::avs_name,
    ds::avisynth::MtMode::NiceFilter
  );
  env->AddFunction("DSAudioTestTone", "i", create_audio_test_tone, nullptr);
  ds::avisynth::set_filter_mt_mode(env, "DSAudioTestTone", ds::avisynth::MtMode::NiceFilter);
  env->AddFunction("DSAudioGain", "cf", create_audio_gain, nullptr);
  ds::avisynth::set_filter_mt_mode(env, "DSAudioGain", ds::avisynth::MtMode::NiceFilter);
  return "DualSynth reference plugin";
}
