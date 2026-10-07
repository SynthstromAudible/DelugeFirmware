# deluge_effects: the effects' words, their settings as plain state, and the codec of an owner element's effect
# entries. Built on the engine API only; serves the Deluge instrument's per-sound effects and the host chains.
add_library(deluge_effects STATIC chain.cpp codec.cpp bitcrush.cpp equalizer.cpp kernels.cpp saturation.cpp settings.cpp)
# src/deluge for the L0 DSP library (dsp-next/) and util/fixedpoint.h; the library boundary check limits which.
target_include_directories(deluge_effects PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../.. ${CMAKE_CURRENT_LIST_DIR}/../../deluge)
target_compile_features(deluge_effects PUBLIC cxx_std_23)
if(DEFINED DELUGE_CXX_STANDARD)
  set_target_properties(deluge_effects PROPERTIES CXX_STANDARD ${DELUGE_CXX_STANDARD} CXX_STANDARD_REQUIRED ON
                                                  CXX_EXTENSIONS ON)
endif()
target_link_libraries(deluge_effects PUBLIC deluge_engine_api)
target_compile_options(deluge_effects PUBLIC ${DELUGE_COMMON_COMPILE_OPTIONS})
