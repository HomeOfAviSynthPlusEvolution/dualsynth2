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
option(
  DS_BUILD_UNVERIFIED_AVISYNTH_POSIX_REFERENCE
  "Allow building the unverified AviSynth+ reference plugin on Linux and macOS"
  OFF
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DualSynthCompilerOptions)
include(FetchContent)
include(CheckCXXSourceCompiles)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

check_cxx_source_compiles([=[
  #include <array>
  #include <cstddef>
  #include <mdspan>

  int main() {
    int storage[4]{};
    std::mdspan<
      int,
      std::dextents<std::size_t, 2>,
      std::layout_stride
    > view{
      storage,
      std::layout_stride::mapping<std::dextents<std::size_t, 2>>{
        std::dextents<std::size_t, 2>{2, 2},
        std::array<std::size_t, 2>{2, 1}
      }
    };

    view[1, 1] = 1;
    return storage[3] == 1 ? 0 : 1;
  }
]=] DS_HAVE_STD_MDSPAN)

if(NOT DS_HAVE_STD_MDSPAN)
  FetchContent_Declare(
    Mdspan
    GIT_REPOSITORY https://github.com/kokkos/mdspan.git
    GIT_TAG stable
  )
  FetchContent_GetProperties(Mdspan)
  if(NOT mdspan_POPULATED)
    if(POLICY CMP0169)
      cmake_policy(PUSH)
      cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(Mdspan)
    if(POLICY CMP0169)
      cmake_policy(POP)
    endif()
  endif()

  add_library(dualsynth_mdspan_polyfill INTERFACE)
  target_include_directories(dualsynth_mdspan_polyfill
    INTERFACE
      "${mdspan_SOURCE_DIR}/include"
  )
  target_compile_definitions(dualsynth_mdspan_polyfill
    INTERFACE
      MDSPAN_USE_BRACKET_OPERATOR=1
      MDSPAN_USE_PAREN_OPERATOR=0
  )
endif()

add_library(dualsynth STATIC
  src/dualsynth/error.cpp
  src/dualsynth/format.cpp
  src/dualsynth/frame.cpp
  src/dualsynth/param.cpp
)

add_library(DualSynth::dualsynth ALIAS dualsynth)

target_compile_features(dualsynth PUBLIC cxx_std_23)
target_include_directories(dualsynth
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
ds_apply_compiler_options(dualsynth)
target_compile_definitions(dualsynth
  PUBLIC
    DS_USE_STD_MDSPAN=$<BOOL:${DS_HAVE_STD_MDSPAN}>
)
if(TARGET dualsynth_mdspan_polyfill)
  target_link_libraries(dualsynth PUBLIC dualsynth_mdspan_polyfill)
endif()

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

  if(DS_VAPOURSYNTH_INCLUDE_DIR)
    add_subdirectory(plugins/vapoursynth/reference)
  endif()
endif()

set(DS_CAN_BUILD_AVISYNTH_REFERENCE OFF)
set(DS_AVISYNTH_REFERENCE_PLATFORM_STATUS "disabled")

if(DS_BUILD_AVISYNTH_REFERENCE)
  if(WIN32)
    if(MSVC)
      set(DS_CAN_BUILD_AVISYNTH_REFERENCE ON)
      set(DS_AVISYNTH_REFERENCE_PLATFORM_STATUS "windows-msvc-verified")
    else()
      set(DS_AVISYNTH_REFERENCE_PLATFORM_STATUS "windows-non-msvc-unsupported")
    endif()
  elseif(APPLE)
    set(DS_AVISYNTH_REFERENCE_PLATFORM_STATUS "macos-unverified")
    if(DS_BUILD_UNVERIFIED_AVISYNTH_POSIX_REFERENCE)
      set(DS_CAN_BUILD_AVISYNTH_REFERENCE ON)
    endif()
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(DS_AVISYNTH_REFERENCE_PLATFORM_STATUS "linux-unverified")
    if(DS_BUILD_UNVERIFIED_AVISYNTH_POSIX_REFERENCE)
      set(DS_CAN_BUILD_AVISYNTH_REFERENCE ON)
    endif()
  elseif(UNIX)
    set(DS_AVISYNTH_REFERENCE_PLATFORM_STATUS "posix-other-unverified")
    if(DS_BUILD_UNVERIFIED_AVISYNTH_POSIX_REFERENCE)
      set(DS_CAN_BUILD_AVISYNTH_REFERENCE ON)
    endif()
  else()
    set(DS_AVISYNTH_REFERENCE_PLATFORM_STATUS "unsupported")
  endif()

  message(STATUS "AviSynth+ reference platform status: ${DS_AVISYNTH_REFERENCE_PLATFORM_STATUS}")
endif()

if(DS_BUILD_AVISYNTH_REFERENCE AND DS_CAN_BUILD_AVISYNTH_REFERENCE)
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
