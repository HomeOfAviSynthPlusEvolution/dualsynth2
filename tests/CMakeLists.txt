include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(dualsynth_unit_tests
  unit/test_error.cpp
  unit/test_fake_frame_lifetime.cpp
  unit/test_format.cpp
  unit/test_param.cpp
  unit/test_mdspan.cpp
  unit/test_video_bridge.cpp
  unit/test_vapoursynth_video_bridge.cpp
  unit/test_acceptance_temporal_average3.cpp
  unit/test_reference_audio_filters.cpp
  unit/test_reference_video_filters.cpp
  unit/test_neo_gradient_mask.cpp
)

target_link_libraries(dualsynth_unit_tests
  PRIVATE
    DualSynth::dualsynth
    Catch2::Catch2WithMain
)

target_compile_features(dualsynth_unit_tests PRIVATE cxx_std_23)
ds_apply_compiler_options(dualsynth_unit_tests)

include(Catch)
catch_discover_tests(dualsynth_unit_tests)

find_program(DS_VSPIPE_EXECUTABLE vspipe)
set(DS_AVSMETER64_ROOT "" CACHE PATH "Optional directory containing AVSMeter64")
set(_ds_avsmeter64_hints)
if(DS_AVSMETER64_ROOT)
  list(APPEND _ds_avsmeter64_hints "${DS_AVSMETER64_ROOT}")
endif()
if(DEFINED ENV{DS_AVSMETER64_ROOT})
  list(APPEND _ds_avsmeter64_hints "$ENV{DS_AVSMETER64_ROOT}")
endif()
find_program(DS_AVSMETER64_EXECUTABLE
  NAMES AVSMeter64 AVSMeter64.exe
  HINTS ${_ds_avsmeter64_hints}
)
unset(_ds_avsmeter64_hints)

if(TARGET dualsynth_reference_vs AND DS_VSPIPE_EXECUTABLE)
  add_test(
    NAME vs_testpattern_info
    COMMAND
      ${DS_VSPIPE_EXECUTABLE}
      --arg plugin=$<TARGET_FILE:dualsynth_reference_vs>
      --info
      ${CMAKE_CURRENT_SOURCE_DIR}/integration/vapoursynth/testpattern_info.vpy
      -
  )

  add_test(
    NAME vs_video_identity_process
    COMMAND
      ${DS_VSPIPE_EXECUTABLE}
      --arg plugin=$<TARGET_FILE:dualsynth_reference_vs>
      ${CMAKE_CURRENT_SOURCE_DIR}/integration/vapoursynth/video_identity_process.vpy
      --
  )

  add_test(
    NAME vs_video_invert_process
    COMMAND
      ${DS_VSPIPE_EXECUTABLE}
      --arg plugin=$<TARGET_FILE:dualsynth_reference_vs>
      ${CMAKE_CURRENT_SOURCE_DIR}/integration/vapoursynth/video_invert_process.vpy
      --
  )

  add_test(
    NAME vs_video_transpose_roundtrip
    COMMAND
      ${DS_VSPIPE_EXECUTABLE}
      --arg plugin=$<TARGET_FILE:dualsynth_reference_vs>
      ${CMAKE_CURRENT_SOURCE_DIR}/integration/vapoursynth/video_transpose_roundtrip.vpy
      --
  )

  add_test(
    NAME vs_acceptance_temporal_average3
    COMMAND
      ${DS_VSPIPE_EXECUTABLE}
      --arg plugin=$<TARGET_FILE:dualsynth_reference_vs>
      ${CMAKE_CURRENT_SOURCE_DIR}/integration/vapoursynth/acceptance_temporal_average3.vpy
      --
  )

  add_test(
    NAME vs_audio_gain_process
    COMMAND
      ${DS_VSPIPE_EXECUTABLE}
      --arg plugin=$<TARGET_FILE:dualsynth_reference_vs>
      ${CMAKE_CURRENT_SOURCE_DIR}/integration/vapoursynth/audio_gain_process.vpy
      --
  )
endif()

if(TARGET dualsynth_reference_avs AND DS_AVSMETER64_EXECUTABLE)
  set(DS_AVISYNTH_TEST_SCRIPT_DIR "${CMAKE_CURRENT_BINARY_DIR}/integration/avisynth")

  file(GENERATE
    OUTPUT "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/testpattern_info.avs"
    CONTENT "LoadPlugin(\"$<TARGET_FILE:dualsynth_reference_avs>\")\nreturn DSTestPattern(16, 16)\n"
  )

  file(GENERATE
    OUTPUT "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/video_invert_process.avs"
    CONTENT "LoadPlugin(\"$<TARGET_FILE:dualsynth_reference_avs>\")\nreturn DSVideoInvert(DSTestPattern(16, 16))\n"
  )

  file(GENERATE
    OUTPUT "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/video_transpose_roundtrip.avs"
    CONTENT "LoadPlugin(\"$<TARGET_FILE:dualsynth_reference_avs>\")\nsrc = DSTestPattern(13, 7)\nreturn DSVideoTranspose(DSVideoTranspose(src))\n"
  )

  file(GENERATE
    OUTPUT "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/acceptance_temporal_average3.avs"
    CONTENT "LoadPlugin(\"$<TARGET_FILE:dualsynth_reference_avs>\")\na = DSTestPattern(16, 16)\nb = DSTestPattern(16, 16)\nc = DSTestPattern(16, 16)\nreturn DSAcceptanceTemporalAverage3(a, b, c).Trim(1, 1)\n"
  )

  file(GENERATE
    OUTPUT "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/audio_gain_info.avs"
    CONTENT "LoadPlugin(\"$<TARGET_FILE:dualsynth_reference_avs>\")\nreturn DSAudioGain(DSAudioTestTone(48000), 0.5)\n"
  )

  add_test(
    NAME avs_testpattern_info
    COMMAND
      ${DS_AVSMETER64_EXECUTABLE}
      "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/testpattern_info.avs"
      -info
  )

  add_test(
    NAME avs_video_invert_process
    COMMAND
      ${DS_AVSMETER64_EXECUTABLE}
      "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/video_invert_process.avs"
      -range=0,0
      -o
  )

  add_test(
    NAME avs_video_transpose_roundtrip
    COMMAND
      ${DS_AVSMETER64_EXECUTABLE}
      "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/video_transpose_roundtrip.avs"
      -range=0,0
      -o
  )

  add_test(
    NAME avs_acceptance_temporal_average3
    COMMAND
      ${DS_AVSMETER64_EXECUTABLE}
      "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/acceptance_temporal_average3.avs"
      -range=0,0
      -o
  )

  add_test(
    NAME avs_audio_gain_info
    COMMAND
      ${DS_AVSMETER64_EXECUTABLE}
      "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/audio_gain_info.avs"
      -info
  )
endif()
