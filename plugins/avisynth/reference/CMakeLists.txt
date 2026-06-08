add_library(dualsynth_reference_avs MODULE
  avs_reference_plugin.cpp
)

target_link_libraries(dualsynth_reference_avs PRIVATE DualSynth::dualsynth)
target_include_directories(dualsynth_reference_avs PRIVATE ${DS_AVISYNTH_INCLUDE_DIR})
target_compile_features(dualsynth_reference_avs PRIVATE cxx_std_20)
ds_apply_compiler_options(dualsynth_reference_avs)

set_target_properties(dualsynth_reference_avs PROPERTIES
  PREFIX ""
  OUTPUT_NAME "dualsynth_reference_avs"
)
