add_library(dualsynth_reference_vs MODULE
  vs_reference_plugin.cpp
)

target_link_libraries(dualsynth_reference_vs PRIVATE DualSynth::dualsynth)
target_include_directories(dualsynth_reference_vs PRIVATE ${DS_VAPOURSYNTH_INCLUDE_DIR})
target_compile_features(dualsynth_reference_vs PRIVATE cxx_std_20)
ds_apply_compiler_options(dualsynth_reference_vs)

if(MINGW)
  target_link_options(dualsynth_reference_vs PRIVATE -static -static-libgcc -static-libstdc++)
endif()

set_target_properties(dualsynth_reference_vs PROPERTIES
  PREFIX ""
  OUTPUT_NAME "dualsynth_reference"
)
