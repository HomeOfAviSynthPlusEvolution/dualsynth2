cmake_minimum_required(VERSION 3.24)

project(DualSynth
  VERSION 0.1.0
  DESCRIPTION "Dual-host modern C++ SDK/runtime for VapourSynth and AviSynth+ plugins"
  LANGUAGES CXX
)

option(DS_BUILD_TESTS "Build DualSynth tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DualSynthCompilerOptions)

add_library(dualsynth STATIC
  src/dualsynth/error.cpp
)

add_library(DualSynth::dualsynth ALIAS dualsynth)

target_compile_features(dualsynth PUBLIC cxx_std_20)
target_include_directories(dualsynth
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
ds_apply_compiler_options(dualsynth)

if(DS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
