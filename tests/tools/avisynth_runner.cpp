#include <avisynth.h>

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
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

const AVS_Linkage* AVS_linkage = nullptr;

namespace {

enum class Mode {
  Info,
  Video,
  Audio
};

struct Options {
  Mode mode = Mode::Info;
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

using CreateScriptEnvironmentFn = IScriptEnvironment*(__stdcall *)(int);

[[noreturn]] void throw_usage() {
  throw std::runtime_error(
    "usage: dualsynth_avisynth_runner (--info|--video|--audio) <script.avs> "
    "[--frame n] [--samples n] [--expect-y8-sum n] [--runtime path]"
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
    if (arg == "--frame") {
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

PClip import_clip(IScriptEnvironment* env, const std::filesystem::path& script) {
  const auto absolute_script = std::filesystem::absolute(script);
  const auto parent = absolute_script.parent_path();
  if (!parent.empty()) {
    const auto parent_string = parent.string();
    env->SetWorkingDir(parent_string.c_str());
  }

  const auto script_string = absolute_script.string();
  AVSValue arg(script_string.c_str());
  AVSValue result = env->Invoke("Import", AVSValue(&arg, 1));
  if (!result.IsClip()) {
    throw std::runtime_error("script did not return an AviSynth clip: " + script_string);
  }
  return result.AsClip();
}

void print_info(const VideoInfo& vi) {
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
}

int64_t checked_audio_bytes(const VideoInfo& vi, int64_t samples) {
  const int64_t bytes = vi.BytesFromAudioSamples(samples);
  if (bytes <= 0) {
    throw std::runtime_error("audio buffer size is not positive");
  }
  if (static_cast<uint64_t>(bytes) > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw std::runtime_error("audio buffer size is too large");
  }
  return bytes;
}

int64_t y8_sum(const PVideoFrame& frame) {
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
  return sum;
}

void run_script(const Options& options, IScriptEnvironment* env) {
  PClip clip = import_clip(env, options.script);
  const VideoInfo& vi = clip->GetVideoInfo();

  switch (options.mode) {
  case Mode::Info:
    print_info(vi);
    return;
  case Mode::Video: {
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
      if (!vi.IsColorSpace(VideoInfo::CS_Y8)) {
        throw std::runtime_error("--expect-y8-sum requires a Y8 clip");
      }
      const int64_t actual = y8_sum(frame);
      if (actual != *options.expect_y8_sum) {
        throw std::runtime_error(
          "unexpected Y8 sum: expected " + std::to_string(*options.expect_y8_sum) +
          " got " + std::to_string(actual)
        );
      }
    }
    std::cout << "video frame=" << options.frame << " ok\n";
    return;
  }
  case Mode::Audio: {
    if (!vi.HasAudio()) {
      throw std::runtime_error("script returned a clip without audio");
    }
    int64_t samples = options.samples;
    if (vi.num_audio_samples > 0 && samples > vi.num_audio_samples) {
      samples = vi.num_audio_samples;
    }
    const int64_t bytes = checked_audio_bytes(vi, samples);
    std::vector<std::byte> buffer(static_cast<std::size_t>(bytes));
    clip->GetAudio(buffer.data(), 0, samples, env);
    std::cout << "audio samples=" << samples << " ok\n";
    return;
  }
  }
}

IScriptEnvironment* create_script_environment(CreateScriptEnvironmentFn create_environment) {
  static constexpr std::array versions{
    AVISYNTH_INTERFACE_VERSION,
    AVISYNTH_CLASSIC_INTERFACE_VERSION
  };

  for (const int version : versions) {
    if (IScriptEnvironment* env = create_environment(version)) {
      return env;
    }
  }

  throw std::runtime_error("CreateScriptEnvironment returned null for all supported versions");
}

void run_with_environment(const Options& options, CreateScriptEnvironmentFn create_environment) {
  IScriptEnvironment* env = create_script_environment(create_environment);

  try {
    AVS_linkage = env->GetAVSLinkage();
    run_script(options, env);
  } catch (...) {
    env->DeleteScriptEnvironment();
    AVS_linkage = nullptr;
    throw;
  }

  env->DeleteScriptEnvironment();
  AVS_linkage = nullptr;
}

} // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parse_args(argc, argv);
    DynamicLibrary runtime = load_avisynth_runtime(options);
    const auto create_environment =
      runtime.symbol<CreateScriptEnvironmentFn>("CreateScriptEnvironment");
    run_with_environment(options, create_environment);
    return EXIT_SUCCESS;
  } catch (const AvisynthError& error) {
    std::cerr << "AviSynth error: " << error.msg << '\n';
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
  }
  return EXIT_FAILURE;
}
