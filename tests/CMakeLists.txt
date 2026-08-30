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
  unit/test_stateful_video_filter.cpp
  unit/test_span2d.cpp
  unit/test_video_bridge.cpp
  unit/test_global_lock.cpp
  unit/test_vapoursynth_video_bridge.cpp
  unit/test_acceptance_copy_stamp.cpp
  unit/test_acceptance_temporal_average3.cpp
  unit/test_reference_audio_filters.cpp
  unit/test_reference_video_filters.cpp
)

target_link_libraries(dualsynth_unit_tests
  PRIVATE
    DualSynth::dualsynth
    Catch2::Catch2WithMain
)

target_compile_features(dualsynth_unit_tests PRIVATE cxx_std_23)
ds_apply_compiler_options(dualsynth_unit_tests)
target_include_directories(dualsynth_unit_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/acceptance/src
)

if(DS_VAPOURSYNTH_INCLUDE_DIR)
  target_include_directories(dualsynth_unit_tests PRIVATE ${DS_VAPOURSYNTH_INCLUDE_DIR})
endif()

if(DS_AVISYNTH_INCLUDE_DIR)
  target_sources(dualsynth_unit_tests
    PRIVATE
      unit/test_avisynth_global_lock.cpp
      unit/test_avisynth_video_bridge.cpp
  )
  target_include_directories(dualsynth_unit_tests PRIVATE ${DS_AVISYNTH_INCLUDE_DIR})
endif()

include(Catch)
catch_discover_tests(dualsynth_unit_tests)

find_program(DS_VSPIPE_EXECUTABLE vspipe)

if(DS_AVISYNTH_INCLUDE_DIR)
  add_executable(dualsynth_avisynth_runner
    tools/avisynth_runner.cpp
  )
  target_compile_features(dualsynth_avisynth_runner PRIVATE cxx_std_23)
  ds_apply_compiler_options(dualsynth_avisynth_runner)
  target_include_directories(dualsynth_avisynth_runner
    PRIVATE
      ${DS_AVISYNTH_INCLUDE_DIR}
  )
  target_link_libraries(dualsynth_avisynth_runner
    PRIVATE
      ${CMAKE_DL_LIBS}
  )
endif()

if(TARGET dualsynth_acceptance AND DS_VSPIPE_EXECUTABLE AND DS_VAPOURSYNTH_INCLUDE_DIR)
  add_test(
    NAME vs_testpattern_info
    COMMAND
      ${DS_VSPIPE_EXECUTABLE}
      --arg plugin=$<TARGET_FILE:dualsynth_acceptance>
      --info
      ${CMAKE_CURRENT_SOURCE_DIR}/integration/vapoursynth/testpattern_info.vpy
      -
  )

  add_test(
    NAME vs_video_identity_process
    COMMAND
      ${DS_VSPIPE_EXECUTABLE}
      --arg plugin=$<TARGET_FILE:dualsynth_acceptance>
      ${CMAKE_CURRENT_SOURCE_DIR}/integration/vapoursynth/video_identity_process.vpy
      --
  )

  add_test(
    NAME vs_video_invert_process
    COMMAND
      ${DS_VSPIPE_EXECUTABLE}
      --arg plugin=$<TARGET_FILE:dualsynth_acceptance>
      ${CMAKE_CURRENT_SOURCE_DIR}/integration/vapoursynth/video_invert_process.vpy
      --
  )

  add_test(
    NAME vs_video_transpose_roundtrip
    COMMAND
      ${DS_VSPIPE_EXECUTABLE}
      --arg plugin=$<TARGET_FILE:dualsynth_acceptance>
      ${CMAKE_CURRENT_SOURCE_DIR}/integration/vapoursynth/video_transpose_roundtrip.vpy
      --
  )

  add_test(
    NAME vs_acceptance_temporal_average3
    COMMAND
      ${DS_VSPIPE_EXECUTABLE}
      --arg plugin=$<TARGET_FILE:dualsynth_acceptance>
      ${CMAKE_CURRENT_SOURCE_DIR}/integration/vapoursynth/acceptance_temporal_average3.vpy
      --
  )

  add_test(
    NAME vs_acceptance_copy_stamp
    COMMAND
      ${DS_VSPIPE_EXECUTABLE}
      --arg plugin=$<TARGET_FILE:dualsynth_acceptance>
      ${CMAKE_CURRENT_SOURCE_DIR}/integration/vapoursynth/acceptance_copy_stamp.vpy
      --
  )

  add_test(
    NAME vs_audio_gain_process
    COMMAND
      ${DS_VSPIPE_EXECUTABLE}
      --arg plugin=$<TARGET_FILE:dualsynth_acceptance>
      ${CMAKE_CURRENT_SOURCE_DIR}/integration/vapoursynth/audio_gain_process.vpy
      --
  )
endif()

if(TARGET dualsynth_acceptance AND TARGET dualsynth_avisynth_runner AND DS_AVISYNTH_INCLUDE_DIR)
  set(DS_AVISYNTH_TEST_SCRIPT_DIR "${CMAKE_CURRENT_BINARY_DIR}/integration/avisynth")

  file(GENERATE
    OUTPUT "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/testpattern_info.avs"
    CONTENT "LoadPlugin(\"$<TARGET_FILE:dualsynth_acceptance>\")\nreturn DSTestPattern(16, 16)\n"
  )

  file(GENERATE
    OUTPUT "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/video_invert_process.avs"
    CONTENT "LoadPlugin(\"$<TARGET_FILE:dualsynth_acceptance>\")\nreturn DSVideoInvert(DSTestPattern(16, 16))\n"
  )

  file(GENERATE
    OUTPUT "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/video_transpose_roundtrip.avs"
    CONTENT "LoadPlugin(\"$<TARGET_FILE:dualsynth_acceptance>\")\nsrc = DSTestPattern(13, 7)\nreturn DSVideoTranspose(DSVideoTranspose(src))\n"
  )

  file(GENERATE
    OUTPUT "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/acceptance_temporal_average3.avs"
    CONTENT "LoadPlugin(\"$<TARGET_FILE:dualsynth_acceptance>\")\na = DSTestPattern(16, 16)\nb = DSTestPattern(16, 16)\nc = DSTestPattern(16, 16)\nreturn DSAcceptanceTemporalAverage3(a, b, c).Trim(1, 1)\n"
  )

  file(GENERATE
    OUTPUT "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/acceptance_copy_stamp.avs"
    CONTENT "LoadPlugin(\"$<TARGET_FILE:dualsynth_acceptance>\")\nreturn DSAcceptanceCopyStamp(DSTestPattern(4, 4))\n"
  )

  file(GENERATE
    OUTPUT "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/audio_gain_info.avs"
    CONTENT "LoadPlugin(\"$<TARGET_FILE:dualsynth_acceptance>\")\nreturn DSAudioGain(DSAudioTestTone(48000), 0.5)\n"
  )

  add_test(
    NAME avs_testpattern_info
    COMMAND
      $<TARGET_FILE:dualsynth_avisynth_runner>
      --info
      "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/testpattern_info.avs"
  )

  add_test(
    NAME avs_video_invert_process
    COMMAND
      $<TARGET_FILE:dualsynth_avisynth_runner>
      --video
      "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/video_invert_process.avs"
      --frame
      0
  )

  add_test(
    NAME avs_video_transpose_roundtrip
    COMMAND
      $<TARGET_FILE:dualsynth_avisynth_runner>
      --video
      "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/video_transpose_roundtrip.avs"
      --frame
      0
  )

  add_test(
    NAME avs_acceptance_temporal_average3
    COMMAND
      $<TARGET_FILE:dualsynth_avisynth_runner>
      --video
      "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/acceptance_temporal_average3.avs"
      --frame
      0
  )

  add_test(
    NAME avs_acceptance_copy_stamp
    COMMAND
      $<TARGET_FILE:dualsynth_avisynth_runner>
      --video
      "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/acceptance_copy_stamp.avs"
      --frame
      1
      --expect-y8-sum
      318
  )

  add_test(
    NAME avs_audio_gain_info
    COMMAND
      $<TARGET_FILE:dualsynth_avisynth_runner>
      --audio
      "${DS_AVISYNTH_TEST_SCRIPT_DIR}/$<CONFIG>/audio_gain_info.avs"
      --samples
      4800
  )
endif()
