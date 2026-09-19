// Standalone host diagnostic: does not load or link a DualSynth plugin.
#include <windows.h>
#include <avisynth_c.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>

template<class T>
T load(HMODULE module, const char* name) {
  auto address = GetProcAddress(module, name);
  if (!address) throw std::runtime_error(name);
  return reinterpret_cast<T>(address);
}

int main(int argc, char** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  const bool control = argc > 1 && std::strcmp(argv[1], "--without-property") == 0;
  const int runtime_arg = control ? 2 : 1;
  HMODULE module = argc > runtime_arg ? LoadLibraryA(argv[runtime_arg]) :
    LoadLibraryExA("avisynth.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!module) { std::fprintf(stderr, "Cannot load AviSynth runtime\n"); return 1; }
  try {
#define HOST_API(name) auto name = load<decltype(&avs_##name)>(module, "avs_" #name)
    HOST_API(create_script_environment);
    HOST_API(new_video_frame_a);
    HOST_API(get_frame_props_rw);
    HOST_API(prop_set_frame);
    HOST_API(release_video_frame);
    HOST_API(delete_script_environment);
#undef HOST_API
    auto* env = create_script_environment(11);
    if (!env) throw std::runtime_error("AviSynth interface 11 is unavailable");
    AVS_VideoInfo vi{};
    vi.width = 16; vi.height = 8; vi.num_frames = 1;
    vi.fps_numerator = 1; vi.fps_denominator = 1; vi.pixel_type = AVS_CS_Y8;
    auto* parent = new_video_frame_a(env, &vi, AVS_FRAME_ALIGN);
    vi.width = 7; vi.height = 3; vi.pixel_type = AVS_CS_Y16;
    auto* auxiliary = new_video_frame_a(env, &vi, AVS_FRAME_ALIGN);
    int error = 1;
    if (parent && auxiliary) {
      error = control ? 0 : prop_set_frame(env, get_frame_props_rw(env, parent), "Aux", auxiliary, 0);
    }
    if (auxiliary) release_video_frame(auxiliary);
    if (parent) release_video_frame(parent);
    std::fprintf(stderr, "Frames released; property status=%d; destroying environment\n", error);
    delete_script_environment(env);
    FreeLibrary(module);
    std::fprintf(stderr, "Environment destruction completed\n");
    return error ? 1 : 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    FreeLibrary(module);
    return 1;
  }
}
