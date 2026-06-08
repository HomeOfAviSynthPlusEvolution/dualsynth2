cmake_minimum_required(VERSION 3.24)

project(DualSynth
  VERSION 0.1.0
  DESCRIPTION "Dual-host modern C++ SDK/runtime for VapourSynth and AviSynth+ plugins"
  LANGUAGES CXX
)

option(DS_BUILD_TESTS "Build DualSynth tests" ON)
option(DS_BUILD_VAPOURSYNTH_REFERENCE "Build VapourSynth reference plugin" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DualSynthCompilerOptions)

add_library(dualsynth STATIC
  src/dualsynth/error.cpp
  src/dualsynth/format.cpp
  src/dualsynth/frame.cpp
  src/dualsynth/param.cpp
)

add_library(DualSynth::dualsynth ALIAS dualsynth)

target_compile_features(dualsynth PUBLIC cxx_std_20)
target_include_directories(dualsynth
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
ds_apply_compiler_options(dualsynth)

if(DS_BUILD_VAPOURSYNTH_REFERENCE)
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

  if(DS_VAPOURSYNTH_INCLUDE_DIR)
    add_subdirectory(plugins/vapoursynth/reference)
  endif()
endif()

if(DS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
