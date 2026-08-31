# DualSynth

DualSynth is a modern C++ wrapper layer for writing video filters that can target both VapourSynth and AviSynth+. The project provides a shared core runtime, host bridge code, and small acceptance plugins used to verify cross-host behavior.

The goal is to let plugin authors keep filter logic in one C++ core while building thin host-specific entry points around it.

## Build

DualSynth uses CMake and requires a C++20 compiler.

```sh
cmake -S . -B build
cmake --build build --config Release
```

Tests and acceptance plugins are enabled by default. They can be disabled with:

```sh
cmake -S . -B build -DDS_BUILD_TESTS=OFF -DDS_BUILD_ACCEPTANCE_PLUGIN=OFF
```

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

On MSVC, DualSynth does not force `/MD` or `/MT`. Use the same runtime library
for the plugin and DualSynth, either by setting `CMAKE_MSVC_RUNTIME_LIBRARY` in
the top-level plugin project or by setting `DS_MSVC_RUNTIME_LIBRARY` before
adding DualSynth.

Write the filter core against the DualSynth C++ API, then provide the VapourSynth and/or AviSynth+ bridge entry points needed by the host.

## License

This project is licensed under the MIT License.
