cmake_minimum_required(VERSION 3.24)

project(DualSynth
  VERSION 0.1.0
  DESCRIPTION "Dual-host modern C++ SDK/runtime for VapourSynth and AviSynth+ plugins"
  LANGUAGES CXX
)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(DS_BUILD_TESTS "Build DualSynth tests" ON)
option(DS_BUILD_VAPOURSYNTH_REFERENCE "Build VapourSynth reference plugin" ON)
option(DS_BUILD_AVISYNTH_REFERENCE "Build AviSynth+ reference plugin" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DualSynthCompilerOptions)
include(FetchContent)

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

if(DS_BUILD_AVISYNTH_REFERENCE AND WIN32 AND MSVC)
  FetchContent_Declare(
    AviSynthPlus
    URL https://github.com/AviSynth/AviSynthPlus/archive/refs/tags/v3.7.5.zip
    URL_HASH SHA256=b60cd796886a91d32e4f495ac29df19f14c99988dfdc5ef002ebb5fc4b1f0042
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
  set(DS_AVISYNTH_INCLUDE_DIR "${avisynthplus_SOURCE_DIR}/avs_core/include")
  add_subdirectory(plugins/avisynth/reference)
endif()

if(DS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
