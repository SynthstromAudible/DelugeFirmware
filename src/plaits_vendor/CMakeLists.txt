add_library(plaits_dsp STATIC
  plaits/dsp/engine/fm_engine.cc
  plaits/dsp/engine/waveshaping_engine.cc
  plaits/dsp/engine/virtual_analog_engine.cc
  plaits/resources.cc
  stmlib/dsp/units.cc
)
target_include_directories(plaits_dsp PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_compile_options(plaits_dsp PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS} -fno-lto -marm $<$<CONFIG:Release,RelWithDebInfo>:-O3> -finline-functions -Wno-unused-parameter)
target_link_libraries(deluge PRIVATE plaits_dsp)
target_link_libraries(deluge_dsp PRIVATE plaits_dsp)
