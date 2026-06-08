#include <avisynth.h>

#include <dualsynth/acceptance/temporal_average3.hpp>
#include <dualsynth/plane_span.hpp>
#include <dualsynth/reference/audio_filters.hpp>
#include <dualsynth/reference/video_filters.hpp>

#include <array>
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

class AVSFrameProvider final : public ds::VideoFrameProvider {
public:
  AVSFrameProvider(const std::array<PClip, 3>& clips, IScriptEnvironment* env)
    : clips_(clips),
      env_(env) {}

  ds::Result<ds::RequestedVideoFrame> get(int input_index, int frame_number) override {
    if (input_index < 0 || input_index >= static_cast<int>(clips_.size())) {
      return ds::Result<ds::RequestedVideoFrame>::failure(
        ds::Error{ds::ErrorCode::InvalidArgument, "AcceptanceTemporalAverage3 input index is out of range"}
      );
    }

    PVideoFrame frame = clips_[static_cast<std::size_t>(input_index)]->GetFrame(frame_number, env_);
    const VideoInfo& vi = clips_[static_cast<std::size_t>(input_index)]->GetVideoInfo();
    frames_.push_back(frame);
    return ds::Result<ds::RequestedVideoFrame>::success(
      ds::RequestedVideoFrame{
        input_index,
        frame_number,
        ds::PlaneSpan<const BYTE>(
          frame->GetReadPtr(PLANAR_Y),
          vi.width,
          vi.height,
          frame->GetPitch(PLANAR_Y)
        )
      }
    );
  }

private:
  std::array<PClip, 3> clips_;
  IScriptEnvironment* env_;
  std::vector<PVideoFrame> frames_;
};

enum class VideoOperation {
  Identity,
  Invert,
  Transpose,
};

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

class VideoFilter final : public GenericVideoFilter {
public:
  VideoFilter(PClip child, VideoOperation operation)
    : GenericVideoFilter(child),
      operation_(operation) {
    if (operation_ == VideoOperation::Transpose) {
      const int width = vi.width;
      vi.width = vi.height;
      vi.height = width;
    }
  }

  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override {
    PVideoFrame src = child->GetFrame(n, env);
    PVideoFrame dst = env->NewVideoFrame(vi);
    const VideoInfo& src_vi = child->GetVideoInfo();

    ds::PlaneSpan<const BYTE> src_plane(
      src->GetReadPtr(PLANAR_Y),
      src_vi.width,
      src_vi.height,
      src->GetPitch(PLANAR_Y)
    );
    ds::PlaneSpan<BYTE> dst_plane(
      dst->GetWritePtr(PLANAR_Y),
      vi.width,
      vi.height,
      dst->GetPitch(PLANAR_Y)
    );

    switch (operation_) {
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

    return dst;
  }

private:
  VideoOperation operation_;
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

class AcceptanceTemporalAverage3Filter final : public IClip {
public:
  explicit AcceptanceTemporalAverage3Filter(std::array<PClip, 3> clips)
    : clips_(clips) {
    vi_ = clips_[1]->GetVideoInfo();
  }

  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override {
    PVideoFrame dst = env->NewVideoFrame(vi_);
    AVSFrameProvider provider(clips_, env);
    ds::VideoProcessContext context{
      n,
      provider,
      ds::PlaneSpan<BYTE>(
        dst->GetWritePtr(PLANAR_Y),
        vi_.width,
        vi_.height,
        dst->GetPitch(PLANAR_Y)
      )
    };

    const auto result = ds::acceptance::AcceptanceTemporalAverage3::process(context);
    if (!result.has_value()) {
      env->ThrowError(result.error().message.c_str());
    }

    return dst;
  }

  bool __stdcall GetParity(int n) override {
    return clips_[1]->GetParity(n);
  }

  void __stdcall GetAudio(void*, int64_t, int64_t, IScriptEnvironment* env) override {
    env->ThrowError("DualSynth reference: DSAcceptanceTemporalAverage3 has no audio");
  }

  int __stdcall SetCacheHints(int, int) override {
    return 0;
  }

  const VideoInfo& __stdcall GetVideoInfo() override {
    return vi_;
  }

private:
  std::array<PClip, 3> clips_;
  VideoInfo vi_{};
};

AVSValue __cdecl create_test_pattern(AVSValue args, void*, IScriptEnvironment* env) {
  const int width = args[0].AsInt();
  const int height = args[1].AsInt();

  if (width <= 0 || height <= 0) {
    env->ThrowError("DualSynth reference: width and height must be positive");
  }

  return new TestPatternClip(width, height);
}

AVSValue create_video_filter(
  AVSValue args,
  IScriptEnvironment* env,
  VideoOperation operation,
  const char* format_error
) {
  PClip clip = args[0].AsClip();
  const VideoInfo& vi = clip->GetVideoInfo();
  if (!vi.HasVideo() || !vi.IsColorSpace(VideoInfo::CS_Y8)) {
    env->ThrowError(format_error);
  }

  return new VideoFilter(clip, operation);
}

AVSValue __cdecl create_video_identity(AVSValue args, void*, IScriptEnvironment* env) {
  return create_video_filter(
    args,
    env,
    VideoOperation::Identity,
    "DualSynth reference: DSVideoIdentity supports only Y8 video"
  );
}

AVSValue __cdecl create_video_invert(AVSValue args, void*, IScriptEnvironment* env) {
  return create_video_filter(
    args,
    env,
    VideoOperation::Invert,
    "DualSynth reference: DSVideoInvert supports only Y8 video"
  );
}

AVSValue __cdecl create_video_transpose(AVSValue args, void*, IScriptEnvironment* env) {
  return create_video_filter(
    args,
    env,
    VideoOperation::Transpose,
    "DualSynth reference: DSVideoTranspose supports only Y8 video"
  );
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
  std::array<PClip, 3> clips{
    args[0].AsClip(),
    args[1].AsClip(),
    args[2].AsClip()
  };

  std::array<ds::VideoInputInfo, ds::acceptance::AcceptanceTemporalAverage3::input_count> input_infos{};
  for (std::size_t i = 0; i < clips.size(); ++i) {
    const VideoInfo& input_vi = clips[i]->GetVideoInfo();
    if (!input_vi.HasVideo() ||
        !input_vi.IsColorSpace(VideoInfo::CS_Y8)) {
      env->ThrowError("DualSynth reference: DSAcceptanceTemporalAverage3 supports only Y8 video");
    }
    input_infos[i] = ds::VideoInputInfo{input_vi.width, input_vi.height, input_vi.num_frames};
  }

  ds::VideoInitContext init_context{input_infos};
  const auto init_result = ds::acceptance::AcceptanceTemporalAverage3::init(init_context);
  if (!init_result.has_value()) {
    env->ThrowError(init_result.error().message.c_str());
  }

  return new AcceptanceTemporalAverage3Filter(clips);
}

} // namespace

DS_AVS_PLUGIN_EXPORT const char* __stdcall AvisynthPluginInit3(
  IScriptEnvironment* env,
  const AVS_Linkage* const vectors
) {
  AVS_linkage = vectors;
  env->AddFunction("DSTestPattern", "ii", create_test_pattern, nullptr);
  env->AddFunction("DSVideoIdentity", "c", create_video_identity, nullptr);
  env->AddFunction("DSVideoInvert", "c", create_video_invert, nullptr);
  env->AddFunction("DSVideoTranspose", "c", create_video_transpose, nullptr);
  env->AddFunction("DSAcceptanceTemporalAverage3", "ccc", create_acceptance_temporal_average3, nullptr);
  env->AddFunction("DSAudioTestTone", "i", create_audio_test_tone, nullptr);
  env->AddFunction("DSAudioGain", "cf", create_audio_gain, nullptr);
  return "DualSynth reference plugin";
}
