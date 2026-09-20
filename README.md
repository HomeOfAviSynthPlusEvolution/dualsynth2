# DualSynth

DualSynth is a modern C++ wrapper layer for writing video filters that can target both VapourSynth and AviSynth+. The project provides a shared core runtime, host bridge code, and small acceptance plugins used to verify cross-host behavior.

The goal is to let plugin authors keep filter logic in one C++ core while building thin host-specific entry points around it.

See [frame services](FRAME_SERVICES.md) for the optional property/auxiliary-frame
API, ownership contracts, and current host validation limits.

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

## AviSynth parameter binding

AviSynth C and C++ readers validate scalar and array element types before
conversion. Integers retain the host's full signed 64-bit range and floating
values retain its double precision. C++ uses `AsLong` when the host linkage
provides it, falling back to `AsInt` for older hosts. Errors identify the
parameter and, for an invalid array element, its zero-based index. Floats also
accept host integers; booleans require actual booleans.

Array parameters default to `AvisynthArrayBinding::Legacy`, preserving the
existing string slot and trailing `name()` slot for non-clip arrays. Clip
arrays already use native arrays. To explicitly bind an array to one named
argument slot, set the final `ParamSpec` field:

```cpp
ds::ParamSpec vectors{"vectors", ds::ParamType::Integer, {}, false, true};
vectors.avs_array_binding = ds::AvisynthArrayBinding::Native;
```

This generates `[vectors].` and accepts `vectors=[1, 2]` or an array in the
corresponding positional slot. It works for integer, float, boolean, string
and clip arrays. The reader requires an actual array, checks each element,
and enforces `required` even though the host signature uses a named slot.
Native arrays consume no trailing slot and can coexist with legacy bindings.
VapourSynth signatures are unaffected.

Omitted values remain absent from `ParamValues`; explicit zero, false and
empty arrays remain present. `VideoInputGroup::provided` likewise distinguishes
omitted clip groups from empty arrays. An explicitly native required clip
array may be empty; the filter decides its meaning. The video bridge still
requires at least one total input and a valid parity source. Legacy required
clip arrays retain their nonempty requirement.

Rebuild the core and bridges together after these C++ descriptor changes.
The framework does not apply int32 saturation, integer-to-boolean conversion,
or downstream default/inheritance rules.

## Plane strides

`Plane<T>`, `RestrictPlane<T>`, `RowCursor<T>` and their factories take strides
in **bytes** when constructed. `stride()` reports elements; `stride_bytes()`
reports bytes. This convention is identical on 32-bit and 64-bit targets.

For example, a `uint16_t` plane with eight elements between rows is constructed
with `Plane<uint16_t>(data, width, height, 16)`. Existing direct constructor calls
that supplied element counts must multiply by `sizeof(T)` when updating DS2.
`make_plane`, `make_restrict_plane` and `as_plane` already used byte strides and
require no call-site changes. Strides must be divisible by `sizeof(T)` and the
resulting element count must fit `int32_t`; views do not validate the backing
allocation. Rebuild consumers with the updated header.

## License

This project is licensed under the MIT License.
