cmake_minimum_required(VERSION 3.24)

project(DualSynth
  VERSION 0.1.0
  DESCRIPTION "Dual-host modern C++ SDK/runtime for VapourSynth and AviSynth+ plugins"
  LANGUAGES CXX
)

option(DS_BUILD_TESTS "Build DualSynth tests" ON)
option(DS_BUILD_ACCEPTANCE_PLUGIN "Build DualSynth acceptance plugin" ON)
option(
  DS_BUILD_AVISYNTH_POSIX_ACCEPTANCE_PLUGIN
  "Build the AviSynth+ acceptance plugin entry on POSIX hosts"
  OFF
)
set(DS_MSVC_RUNTIME_LIBRARY "" CACHE STRING
  "Optional MSVC runtime library override for DualSynth targets. Leave empty to use the toolchain or parent project default."
)
set_property(CACHE DS_MSVC_RUNTIME_LIBRARY PROPERTY STRINGS
  ""
  "MultiThreaded"
  "MultiThreadedDebug"
  "MultiThreadedDLL"
  "MultiThreadedDebugDLL"
  "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DualSynthCompilerOptions)
include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(dualsynth STATIC
  src/dualsynth/error.cpp
  src/dualsynth/format.cpp
  src/dualsynth/frame.cpp
  src/dualsynth/param.cpp
  src/dualsynth/video_bridge.cpp
)

add_library(DualSynth::dualsynth ALIAS dualsynth)

set_target_properties(dualsynth PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
if(MSVC AND DS_MSVC_RUNTIME_LIBRARY)
  set_property(TARGET dualsynth PROPERTY
    MSVC_RUNTIME_LIBRARY "${DS_MSVC_RUNTIME_LIBRARY}"
  )
endif()

target_compile_features(dualsynth PUBLIC cxx_std_17)
target_include_directories(dualsynth
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
ds_apply_compiler_options(dualsynth)

if(DS_BUILD_ACCEPTANCE_PLUGIN)
  set(DS_VAPOURSYNTH_SDK "" CACHE PATH "Optional path to a VapourSynth SDK")
  set(_ds_vapoursynth_include_hints)
  if(DS_VAPOURSYNTH_SDK)
    list(APPEND _ds_vapoursynth_include_hints
      "${DS_VAPOURSYNTH_SDK}/include"
      "${DS_VAPOURSYNTH_SDK}"
    )
  endif()
  if(DEFINED ENV{VAPOURSYNTH_SDK})
    list(APPEND _ds_vapoursynth_include_hints
      "$ENV{VAPOURSYNTH_SDK}/include"
      "$ENV{VAPOURSYNTH_SDK}"
    )
  endif()
  find_path(DS_VAPOURSYNTH_INCLUDE_DIR
    NAMES vapoursynth/VapourSynth4.h
    HINTS ${_ds_vapoursynth_include_hints}
  )
  unset(_ds_vapoursynth_include_hints)

  if(NOT DS_VAPOURSYNTH_INCLUDE_DIR)
    FetchContent_Declare(
      VapourSynth
      URL https://github.com/vapoursynth/vapoursynth/archive/refs/tags/R73.zip
      URL_HASH SHA256=7c6b1eb2ec4aeae078675a29cf5e77c6ca1ab8b1dd322677c66e1ca6b76c511d
    )
    FetchContent_GetProperties(VapourSynth)
    if(NOT vapoursynth_POPULATED)
      if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
      endif()
      FetchContent_Populate(VapourSynth)
      if(POLICY CMP0169)
        cmake_policy(POP)
      endif()
    endif()

    set(_ds_vapoursynth_sdk_include_dir "${CMAKE_CURRENT_BINARY_DIR}/_deps/vapoursynth-sdk/include")
    file(MAKE_DIRECTORY "${_ds_vapoursynth_sdk_include_dir}/vapoursynth")
    configure_file(
      "${vapoursynth_SOURCE_DIR}/include/VapourSynth4.h"
      "${_ds_vapoursynth_sdk_include_dir}/vapoursynth/VapourSynth4.h"
      COPYONLY
    )
    set(DS_VAPOURSYNTH_INCLUDE_DIR
      "${_ds_vapoursynth_sdk_include_dir}"
      CACHE PATH "Path to a directory containing vapoursynth/VapourSynth4.h"
      FORCE
    )
    unset(_ds_vapoursynth_sdk_include_dir)
  endif()
endif()

set(DS_AVISYNTH_SDK "" CACHE PATH "Optional path to an AviSynth+ SDK")
set(_ds_avisynth_include_hints)
if(DS_AVISYNTH_SDK)
  list(APPEND _ds_avisynth_include_hints
    "${DS_AVISYNTH_SDK}/include"
    "${DS_AVISYNTH_SDK}/avs_core/include"
    "${DS_AVISYNTH_SDK}"
  )
endif()
if(DEFINED ENV{AVISYNTH_SDK})
  list(APPEND _ds_avisynth_include_hints
    "$ENV{AVISYNTH_SDK}/include"
    "$ENV{AVISYNTH_SDK}/avs_core/include"
    "$ENV{AVISYNTH_SDK}"
  )
endif()
find_path(DS_AVISYNTH_INCLUDE_DIR
  NAMES avisynth_c.h avisynth.h
  HINTS ${_ds_avisynth_include_hints}
)
unset(_ds_avisynth_include_hints)

set(DS_CAN_BUILD_AVISYNTH_ACCEPTANCE_PLUGIN ON)
set(DS_AVISYNTH_ACCEPTANCE_PLATFORM_STATUS "enabled")

if(DS_BUILD_ACCEPTANCE_PLUGIN AND NOT DS_AVISYNTH_INCLUDE_DIR)
  FetchContent_Declare(
    AviSynthPlus
    URL https://github.com/AviSynth/AviSynthPlus/archive/refs/heads/master.zip
  )
  FetchContent_GetProperties(AviSynthPlus)
  if(NOT avisynthplus_POPULATED)
    if(POLICY CMP0169)
      cmake_policy(PUSH)
      cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(AviSynthPlus)
    if(POLICY CMP0169)
      cmake_policy(POP)
    endif()
  endif()
  set(DS_AVISYNTH_INCLUDE_DIR
    "${avisynthplus_SOURCE_DIR}/avs_core/include"
    CACHE PATH "Path to a directory containing avisynth.h and avisynth_c.h"
    FORCE
  )
endif()

if(DS_BUILD_ACCEPTANCE_PLUGIN AND (DS_VAPOURSYNTH_INCLUDE_DIR OR DS_AVISYNTH_INCLUDE_DIR))
  add_subdirectory(plugins/acceptance)
endif()

if(DS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
