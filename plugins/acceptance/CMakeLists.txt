set(dualsynth_acceptance_sources)

if(DS_VAPOURSYNTH_INCLUDE_DIR)
  list(APPEND dualsynth_acceptance_sources src/vapoursynth_entry.cpp)
endif()

if(DS_AVISYNTH_INCLUDE_DIR)
  list(APPEND dualsynth_acceptance_sources src/avisynth_entry.cpp)
endif()

if(NOT dualsynth_acceptance_sources)
  message(FATAL_ERROR "DualSynth acceptance plugin requires at least one host SDK")
endif()

add_library(dualsynth_acceptance MODULE
  ${dualsynth_acceptance_sources}
)

target_link_libraries(dualsynth_acceptance PRIVATE DualSynth::dualsynth)
target_include_directories(dualsynth_acceptance
  PRIVATE
    src
    ${DS_VAPOURSYNTH_INCLUDE_DIR}
    ${DS_AVISYNTH_INCLUDE_DIR}
)
target_compile_features(dualsynth_acceptance PRIVATE cxx_std_20)
ds_apply_compiler_options(dualsynth_acceptance)

if(MSVC AND DS_MSVC_RUNTIME_LIBRARY)
  set_property(TARGET dualsynth_acceptance PROPERTY
    MSVC_RUNTIME_LIBRARY "${DS_MSVC_RUNTIME_LIBRARY}"
  )
endif()

if(MINGW)
  target_link_options(dualsynth_acceptance PRIVATE -static -static-libgcc -static-libstdc++)
endif()

set_target_properties(dualsynth_acceptance PROPERTIES
  PREFIX ""
  OUTPUT_NAME "dualsynth_acceptance"
)
