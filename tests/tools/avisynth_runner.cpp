#include <avisynth_c.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

enum class Mode {
  Info,
  Video,
  Audio
};

enum class Backend {
  Auto,
  C,
  Cpp
};

struct Options {
  Mode mode = Mode::Info;
  Backend backend = Backend::Auto;
  std::filesystem::path script;
  std::string runtime;
  int frame = 0;
  int64_t samples = 4800;
  std::optional<int64_t> expect_y8_sum;
};

class DynamicLibrary {
public:
  DynamicLibrary() = default;

  DynamicLibrary(const DynamicLibrary&) = delete;
  DynamicLibrary& operator=(const DynamicLibrary&) = delete;

  DynamicLibrary(DynamicLibrary&& other) noexcept
    : handle_(other.handle_) {
    other.handle_ = nullptr;
  }

  DynamicLibrary& operator=(DynamicLibrary&& other) noexcept {
    if (this != &other) {
      close();
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  ~DynamicLibrary() {
    close();
  }

  static DynamicLibrary open(const std::string& path) {
    DynamicLibrary library;
#if defined(_WIN32)
    library.handle_ = LoadLibraryA(path.c_str());
    if (!library.handle_) {
      throw std::runtime_error("failed to load " + path);
    }
#else
    library.handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library.handle_) {
      const char* error = dlerror();
      throw std::runtime_error(
        "failed to load " + path + ": " + (error ? error : "unknown dlopen error")
      );
    }
#endif
    return library;
  }

  template <class Function>
  Function symbol(const char* name) const {
#if defined(_WIN32)
    auto* pointer = reinterpret_cast<Function>(GetProcAddress(static_cast<HMODULE>(handle_), name));
#else
    dlerror();
    auto* pointer = reinterpret_cast<Function>(dlsym(handle_, name));
#endif
    if (!pointer) {
#if defined(_WIN32)
      throw std::runtime_error(std::string{"failed to load symbol "} + name);
#else
      const char* error = dlerror();
      throw std::runtime_error(
        std::string{"failed to load symbol "} + name + ": " + (error ? error : "unknown dlsym error")
      );
#endif
    }
    return pointer;
  }

private:
  void close() {
    if (!handle_) {
      return;
    }
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
  }

#if defined(_WIN32)
  HMODULE handle_ = nullptr;
#else
  void* handle_ = nullptr;
#endif
};

struct CApiRunner {
  using avs_create_script_environment_fn = AVS_ScriptEnvironment*(AVSC_CC*)(int);
  using avs_delete_script_environment_fn = void(AVSC_CC*)(AVS_ScriptEnvironment*);
  using avs_set_working_dir_fn = int(AVSC_CC*)(AVS_ScriptEnvironment*, const char*);
  using avs_invoke_fn = AVS_Value(AVSC_CC*)(AVS_ScriptEnvironment*, const char*, AVS_Value, const char**);
  using avs_take_clip_fn = AVS_Clip*(AVSC_CC*)(AVS_Value, AVS_ScriptEnvironment*);
  using avs_release_clip_fn = void(AVSC_CC*)(AVS_Clip*);
  using avs_release_value_fn = void(AVSC_CC*)(AVS_Value);
  using avs_get_video_info_fn = const AVS_VideoInfo*(AVSC_CC*)(AVS_Clip*);
  using avs_get_frame_fn = AVS_VideoFrame*(AVSC_CC*)(AVS_Clip*, int);
  using avs_release_video_frame_fn = void(AVSC_CC*)(AVS_VideoFrame*);
  using avs_get_read_ptr_p_fn = const BYTE*(AVSC_CC*)(const AVS_VideoFrame*, int);
  using avs_get_pitch_p_fn = int(AVSC_CC*)(const AVS_VideoFrame*, int);
  using avs_get_row_size_p_fn = int(AVSC_CC*)(const AVS_VideoFrame*, int);
  using avs_get_height_p_fn = int(AVSC_CC*)(const AVS_VideoFrame*, int);
  using avs_get_audio_fn = int(AVSC_CC*)(AVS_Clip*, void*, int64_t, int64_t);

  avs_create_script_environment_fn create_env{nullptr};
  avs_delete_script_environment_fn delete_env{nullptr};
  avs_set_working_dir_fn set_working_dir{nullptr};
  avs_invoke_fn invoke{nullptr};
  avs_take_clip_fn take_clip{nullptr};
  avs_release_clip_fn release_clip{nullptr};
  avs_release_value_fn release_value{nullptr};
  avs_get_video_info_fn get_video_info{nullptr};
  avs_get_frame_fn get_frame{nullptr};
  avs_release_video_frame_fn release_video_frame{nullptr};
  avs_get_read_ptr_p_fn get_read_ptr_p{nullptr};
  avs_get_pitch_p_fn get_pitch_p{nullptr};
  avs_get_row_size_p_fn get_row_size_p{nullptr};
  avs_get_height_p_fn get_height_p{nullptr};
  avs_get_audio_fn get_audio{nullptr};

  explicit CApiRunner(const DynamicLibrary& lib) {
    create_env = lib.symbol<avs_create_script_environment_fn>("avs_create_script_environment");
    delete_env = lib.symbol<avs_delete_script_environment_fn>("avs_delete_script_environment");
    set_working_dir = lib.symbol<avs_set_working_dir_fn>("avs_set_working_dir");
    invoke = lib.symbol<avs_invoke_fn>("avs_invoke");
    take_clip = lib.symbol<avs_take_clip_fn>("avs_take_clip");
    release_clip = lib.symbol<avs_release_clip_fn>("avs_release_clip");
    release_value = lib.symbol<avs_release_value_fn>("avs_release_value");
    get_video_info = lib.symbol<avs_get_video_info_fn>("avs_get_video_info");
    get_frame = lib.symbol<avs_get_frame_fn>("avs_get_frame");
    release_video_frame = lib.symbol<avs_release_video_frame_fn>("avs_release_video_frame");
    get_read_ptr_p = lib.symbol<avs_get_read_ptr_p_fn>("avs_get_read_ptr_p");
    get_pitch_p = lib.symbol<avs_get_pitch_p_fn>("avs_get_pitch_p");
    get_row_size_p = lib.symbol<avs_get_row_size_p_fn>("avs_get_row_size_p");
    get_height_p = lib.symbol<avs_get_height_p_fn>("avs_get_height_p");
    get_audio = lib.symbol<avs_get_audio_fn>("avs_get_audio");
  }
};

[[noreturn]] void throw_usage() {
  throw std::runtime_error(
    "usage: dualsynth_avisynth_runner (--info|--video|--audio) <script.avs> "
    "[--backend (c|cpp)] [--frame n] [--samples n] [--expect-y8-sum n] [--runtime path]"
  );
}

int parse_int(std::string_view text, const char* name) {
  std::size_t used = 0;
  int value = 0;
  try {
    value = std::stoi(std::string{text}, &used, 10);
  } catch (const std::exception&) {
    throw std::runtime_error(std::string{"invalid "} + name + ": " + std::string{text});
  }
  if (used != text.size()) {
    throw std::runtime_error(std::string{"invalid "} + name + ": " + std::string{text});
  }
  return value;
}

int64_t parse_int64(std::string_view text, const char* name) {
  std::size_t used = 0;
  int64_t value = 0;
  try {
    value = std::stoll(std::string{text}, &used, 10);
  } catch (const std::exception&) {
    throw std::runtime_error(std::string{"invalid "} + name + ": " + std::string{text});
  }
  if (used != text.size()) {
    throw std::runtime_error(std::string{"invalid "} + name + ": " + std::string{text});
  }
  return value;
}

Options parse_args(int argc, char** argv) {
  if (argc < 3) {
    throw_usage();
  }

  Options options;
  const std::string_view mode = argv[1];
  if (mode == "--info") {
    options.mode = Mode::Info;
  } else if (mode == "--video") {
    options.mode = Mode::Video;
  } else if (mode == "--audio") {
    options.mode = Mode::Audio;
  } else {
    throw_usage();
  }

  options.script = argv[2];

  for (int i = 3; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--backend") {
      if (++i >= argc) {
        throw_usage();
      }
      const std::string_view b = argv[i];
      if (b == "c") {
        options.backend = Backend::C;
      } else if (b == "cpp") {
        options.backend = Backend::Cpp;
      } else {
        throw std::runtime_error("invalid backend: " + std::string(b));
      }
    } else if (arg == "--frame") {
      if (++i >= argc) {
        throw_usage();
      }
      options.frame = parse_int(argv[i], "frame");
    } else if (arg == "--samples") {
      if (++i >= argc) {
        throw_usage();
      }
      options.samples = parse_int64(argv[i], "samples");
    } else if (arg == "--expect-y8-sum") {
      if (++i >= argc) {
        throw_usage();
      }
      options.expect_y8_sum = parse_int64(argv[i], "expect-y8-sum");
    } else if (arg == "--runtime") {
      if (++i >= argc) {
        throw_usage();
      }
      options.runtime = argv[i];
    } else {
      throw_usage();
    }
  }

  if (options.script.empty()) {
    throw std::runtime_error("script path is empty");
  }
  if (options.frame < 0) {
    throw std::runtime_error("frame must be non-negative");
  }
  if (options.samples <= 0) {
    throw std::runtime_error("samples must be positive");
  }
  return options;
}

std::vector<std::string> runtime_candidates(const Options& options) {
  if (!options.runtime.empty()) {
    return {options.runtime};
  }

#if defined(_WIN32)
  return {"avisynth.dll"};
#elif defined(__APPLE__)
  return {"libavisynth.dylib", "libavisynth.so", "libavisynth.so.11"};
#else
  return {"libavisynth.so", "libavisynth.so.11"};
#endif
}

DynamicLibrary load_avisynth_runtime(const Options& options) {
  std::string errors;
  for (const auto& candidate : runtime_candidates(options)) {
    try {
      return DynamicLibrary::open(candidate);
    } catch (const std::exception& error) {
      if (!errors.empty()) {
        errors += "; ";
      }
      errors += error.what();
    }
  }
  throw std::runtime_error("could not load AviSynth+ runtime: " + errors);
}

void run_with_c_environment(const Options& options, const DynamicLibrary& runtime) {
  CApiRunner c_api(runtime);
  AVS_ScriptEnvironment* env = nullptr;
  static constexpr std::array versions{12, 11, 8, 6};
  for (int v : versions) {
    env = c_api.create_env(v);
    if (env) {
      break;
    }
  }
  if (!env) {
    throw std::runtime_error("avs_create_script_environment returned null for all supported versions");
  }

  try {
    const auto absolute_script = std::filesystem::absolute(options.script);
    const auto parent = absolute_script.parent_path();
    if (!parent.empty()) {
      c_api.set_working_dir(env, parent.string().c_str());
    }

    const auto script_string = absolute_script.string();
    AVS_Value arg;
    arg.type = 's';
    arg.array_size = 1;
    arg.d.string = script_string.c_str();

    AVS_Value args;
    args.type = 'a';
    args.array_size = 1;
    args.d.array = &arg;

    AVS_Value result = c_api.invoke(env, "Import", args, nullptr);
    if (result.type == 'e') {
      std::string err = result.d.string ? result.d.string : "unknown import error";
      c_api.release_value(result);
      throw std::runtime_error("AviSynth script error: " + err);
    }
    if (result.type != 'c' || !result.d.clip) {
      c_api.release_value(result);
      throw std::runtime_error("script did not return an AviSynth clip: " + script_string);
    }

    AVS_Clip* clip = c_api.take_clip(result, env);
    c_api.release_value(result);
    if (!clip) {
      throw std::runtime_error("failed to take clip from script result");
    }

    const AVS_VideoInfo* vi = c_api.get_video_info(clip);
    if (!vi) {
      c_api.release_clip(clip);
      throw std::runtime_error("get_video_info returned null");
    }

    if (options.mode == Mode::Info) {
      std::cout << "video=" << (vi->width != 0 ? "yes" : "no");
      if (vi->width != 0) {
        std::cout << " width=" << vi->width
                  << " height=" << vi->height
                  << " frames=" << vi->num_frames
                  << " fps=" << vi->fps_numerator << "/" << vi->fps_denominator
                  << " pixel_type=" << vi->pixel_type;
      }
      std::cout << " audio=" << (vi->audio_samples_per_second != 0 ? "yes" : "no");
      if (vi->audio_samples_per_second != 0) {
        std::cout << " sample_rate=" << vi->audio_samples_per_second
                  << " channels=" << vi->nchannels
                  << " sample_type=" << vi->sample_type
                  << " samples=" << vi->num_audio_samples;
      }
      std::cout << '\n';
      c_api.release_clip(clip);
      c_api.delete_env(env);
      return;
    }

    if (options.mode == Mode::Video) {
      if (vi->width == 0) {
        c_api.release_clip(clip);
        throw std::runtime_error("script returned a clip without video");
      }
      if (options.frame >= vi->num_frames) {
        c_api.release_clip(clip);
        throw std::runtime_error("requested frame is outside the clip frame range");
      }
      AVS_VideoFrame* frame = c_api.get_frame(clip, options.frame);
      if (!frame) {
        c_api.release_clip(clip);
        throw std::runtime_error("get_frame returned null");
      }
      if (options.expect_y8_sum.has_value()) {
        const BYTE* src = c_api.get_read_ptr_p(frame, AVS_PLANAR_Y);
        const int pitch = c_api.get_pitch_p(frame, AVS_PLANAR_Y);
        const int row_size = c_api.get_row_size_p(frame, AVS_PLANAR_Y);
        const int height = c_api.get_height_p(frame, AVS_PLANAR_Y);

        int64_t sum = 0;
        for (int y = 0; y < height; ++y) {
          const BYTE* row = src + static_cast<std::ptrdiff_t>(y) * pitch;
          for (int x = 0; x < row_size; ++x) {
            sum += row[x];
          }
        }
        if (sum != *options.expect_y8_sum) {
          c_api.release_video_frame(frame);
          c_api.release_clip(clip);
          throw std::runtime_error(
            "unexpected Y8 sum: expected " + std::to_string(*options.expect_y8_sum) +
            " got " + std::to_string(sum)
          );
        }
      }
      c_api.release_video_frame(frame);
      c_api.release_clip(clip);
      c_api.delete_env(env);
      std::cout << "video frame=" << options.frame << " ok\n";
      return;
    }

    if (options.mode == Mode::Audio) {
      if (vi->audio_samples_per_second == 0) {
        c_api.release_clip(clip);
        throw std::runtime_error("script returned a clip without audio");
      }
      int64_t samples = options.samples;
      if (vi->num_audio_samples > 0 && samples > vi->num_audio_samples) {
        samples = vi->num_audio_samples;
      }
      int sample_bytes = sizeof(float);
      const int64_t bytes = samples * vi->nchannels * sample_bytes;
      std::vector<std::byte> buffer(static_cast<std::size_t>(bytes));
      c_api.get_audio(clip, buffer.data(), 0, samples);
      c_api.release_clip(clip);
      c_api.delete_env(env);
      std::cout << "audio samples=" << samples << " ok\n";
      return;
    }
  } catch (...) {
    c_api.delete_env(env);
    throw;
  }
}

#if defined(_MSC_VER)
#include <avisynth.h>
const AVS_Linkage* AVS_linkage = nullptr;

using CreateScriptEnvironmentFn = IScriptEnvironment*(__stdcall *)(int);

PClip import_clip_cpp(IScriptEnvironment* env, const std::filesystem::path& script) {
  const auto absolute_script = std::filesystem::absolute(script);
  const auto parent = absolute_script.parent_path();
  if (!parent.empty()) {
    env->SetWorkingDir(parent.string().c_str());
  }

  const auto script_string = absolute_script.string();
  AVSValue arg(script_string.c_str());
  AVSValue result = env->Invoke("Import", AVSValue(&arg, 1));
  if (!result.IsClip()) {
    throw std::runtime_error("script did not return an AviSynth clip: " + script_string);
  }
  return result.AsClip();
}

void run_with_cpp_environment(const Options& options, const DynamicLibrary& runtime) {
  const auto create_environment =
    runtime.symbol<CreateScriptEnvironmentFn>("CreateScriptEnvironment");
  static constexpr std::array versions{
    AVISYNTH_INTERFACE_VERSION,
    AVISYNTH_CLASSIC_INTERFACE_VERSION
  };
  IScriptEnvironment* env = nullptr;
  for (const int version : versions) {
    env = create_environment(version);
    if (env) break;
  }
  if (!env) {
    throw std::runtime_error("CreateScriptEnvironment returned null for all supported versions");
  }

  try {
    AVS_linkage = env->GetAVSLinkage();
    PClip clip = import_clip_cpp(env, options.script);
    const VideoInfo& vi = clip->GetVideoInfo();

    if (options.mode == Mode::Info) {
      std::cout << "video=" << (vi.HasVideo() ? "yes" : "no");
      if (vi.HasVideo()) {
        std::cout << " width=" << vi.width
                  << " height=" << vi.height
                  << " frames=" << vi.num_frames
                  << " fps=" << vi.fps_numerator << "/" << vi.fps_denominator
                  << " pixel_type=" << vi.pixel_type;
      }
      std::cout << " audio=" << (vi.HasAudio() ? "yes" : "no");
      if (vi.HasAudio()) {
        std::cout << " sample_rate=" << vi.SamplesPerSecond()
                  << " channels=" << vi.AudioChannels()
                  << " sample_type=" << vi.SampleType()
                  << " samples=" << vi.num_audio_samples;
      }
      std::cout << '\n';
      env->DeleteScriptEnvironment();
      AVS_linkage = nullptr;
      return;
    }

    if (options.mode == Mode::Video) {
      if (!vi.HasVideo()) {
        throw std::runtime_error("script returned a clip without video");
      }
      if (options.frame >= vi.num_frames) {
        throw std::runtime_error("requested frame is outside the clip frame range");
      }
      PVideoFrame frame = clip->GetFrame(options.frame, env);
      if (!frame) {
        throw std::runtime_error("GetFrame returned an empty frame");
      }
      if (options.expect_y8_sum.has_value()) {
        const BYTE* src = frame->GetReadPtr(PLANAR_Y);
        const int pitch = frame->GetPitch(PLANAR_Y);
        const int row_size = frame->GetRowSize(PLANAR_Y);
        const int height = frame->GetHeight(PLANAR_Y);

        int64_t sum = 0;
        for (int y = 0; y < height; ++y) {
          const BYTE* row = src + static_cast<std::ptrdiff_t>(y) * pitch;
          for (int x = 0; x < row_size; ++x) {
            sum += row[x];
          }
        }
        if (sum != *options.expect_y8_sum) {
          throw std::runtime_error(
            "unexpected Y8 sum: expected " + std::to_string(*options.expect_y8_sum) +
            " got " + std::to_string(sum)
          );
        }
      }
      env->DeleteScriptEnvironment();
      AVS_linkage = nullptr;
      std::cout << "video frame=" << options.frame << " ok\n";
      return;
    }

    if (options.mode == Mode::Audio) {
      if (!vi.HasAudio()) {
        throw std::runtime_error("script returned a clip without audio");
      }
      int64_t samples = options.samples;
      if (vi.num_audio_samples > 0 && samples > vi.num_audio_samples) {
        samples = vi.num_audio_samples;
      }
      const int64_t bytes = vi.BytesFromAudioSamples(samples);
      std::vector<std::byte> buffer(static_cast<std::size_t>(bytes));
      clip->GetAudio(buffer.data(), 0, samples, env);
      env->DeleteScriptEnvironment();
      AVS_linkage = nullptr;
      std::cout << "audio samples=" << samples << " ok\n";
      return;
    }
  } catch (...) {
    env->DeleteScriptEnvironment();
    AVS_linkage = nullptr;
    throw;
  }
}
#else
void run_with_cpp_environment(const Options&, const DynamicLibrary&) {
  throw std::runtime_error("C++ AviSynth backend requires MSVC on Windows due to C++ ABI differences");
}
#endif

} // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parse_args(argc, argv);
    DynamicLibrary runtime = load_avisynth_runtime(options);
    if (options.backend == Backend::Cpp) {
      run_with_cpp_environment(options, runtime);
    } else {
      run_with_c_environment(options, runtime);
    }
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
  }
  return EXIT_FAILURE;
}
