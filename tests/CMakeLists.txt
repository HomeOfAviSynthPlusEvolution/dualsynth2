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
  unit/test_plane_span.cpp
  unit/test_reference_audio_filters.cpp
  unit/test_reference_video_filters.cpp
)

target_link_libraries(dualsynth_unit_tests
  PRIVATE
    DualSynth::dualsynth
    Catch2::Catch2WithMain
)

target_compile_features(dualsynth_unit_tests PRIVATE cxx_std_20)
ds_apply_compiler_options(dualsynth_unit_tests)

include(Catch)
catch_discover_tests(dualsynth_unit_tests)

find_program(DS_VSPIPE_EXECUTABLE vspipe)

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
    NAME vs_audio_gain_process
    COMMAND
      ${DS_VSPIPE_EXECUTABLE}
      --arg plugin=$<TARGET_FILE:dualsynth_reference_vs>
      ${CMAKE_CURRENT_SOURCE_DIR}/integration/vapoursynth/audio_gain_process.vpy
      --
  )
endif()
