# DualSynth

DualSynth is a modern C++ wrapper layer for writing video filters that can target both VapourSynth and AviSynth+. The project provides a shared core runtime, host bridge code, and small acceptance plugins used to verify cross-host behavior.

The goal is to let plugin authors keep filter logic in one C++ core while building thin host-specific entry points around it.

## Build

DualSynth uses CMake and requires a C++17 compiler.

```sh
cmake -S . -B build
cmake --build build --config Release
```

Tests and acceptance plugins are enabled by default. They can be disabled with:

```sh
cmake -S . -B build -DDS_BUILD_TESTS=OFF -DDS_BUILD_ACCEPTANCE_PLUGIN=OFF
```

Both hosts are enabled by default. For a VapourSynth-only build (for example,
when packaging a downstream plugin for PyPI), use:

```sh
cmake -S . -B build -DDS_ENABLE_AVISYNTH=OFF
```

Use `-DDS_ENABLE_VAPOURSYNTH=OFF` for an AviSynth-only build. Disabling both
hosts is a configuration error. A disabled host's SDK is not searched for or
downloaded, and its acceptance entry, host-specific tests, and tools are omitted,
even when an SDK path remains cached from an earlier configuration.
Shared C++ APIs and signature helpers remain available in all builds.

## Use

Plugin projects can consume DualSynth with CMake `FetchContent`:

```cmake
include(FetchContent)

set(DS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DS_BUILD_ACCEPTANCE_PLUGIN OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  dualsynth2
  GIT_REPOSITORY https://github.com/HomeOfAviSynthPlusEvolution/dualsynth2.git
  GIT_TAG master
)
FetchContent_MakeAvailable(dualsynth2)

target_link_libraries(my_plugin PRIVATE DualSynth::dualsynth)
```

These options control DualSynth's own targets. Downstream plugins must also
select their entry sources using the same options after adding DualSynth:

```cmake
if(DS_ENABLE_VAPOURSYNTH)
  target_sources(my_plugin PRIVATE src/vapoursynth_entry.cpp)
  # Add the VapourSynth SDK include directory here.
endif()
if(DS_ENABLE_AVISYNTH)
  target_sources(my_plugin PRIVATE src/avisynth_entry.cpp)
  # Add the AviSynth SDK include directory here.
endif()
```

Keep downstream SDK discovery and download steps inside the corresponding
conditions as well. These are CMake options, not C++ preprocessor definitions.

On MSVC, DualSynth does not force `/MD` or `/MT`. Use the same runtime library
for the plugin and DualSynth, either by setting `CMAKE_MSVC_RUNTIME_LIBRARY` in
the top-level plugin project or by setting `DS_MSVC_RUNTIME_LIBRARY` before
adding DualSynth.

Write the filter core against the DualSynth C++ API, then provide the VapourSynth and/or AviSynth+ bridge entry points needed by the host.

## License

This project is licensed under the MIT License.
