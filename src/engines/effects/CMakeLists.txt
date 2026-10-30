# deluge_effects: the effects' words, their settings as plain state, the codec of an owner element's effect
# entries, and the host chains' effect components. Built on the engine API and the L0 DSP library (dsp-next); serves the
# Deluge instrument's per-sound effects and the host chains.
add_library(deluge_effects STATIC chain.cpp chain_core.cpp clip_chain.cpp master_chain.cpp sound_chain.cpp codec.cpp bitcrush.cpp compressor.cpp delay.cpp delay_arena.cpp filter.cpp granular.cpp mod_fx.cpp reverb.cpp saturation.cpp stutter.cpp send_and_volume.cpp settings.cpp sidechain.cpp)
# src/deluge for the L0 DSP library (dsp-next/) and util/fixedpoint.h; the library boundary check limits which.
target_include_directories(deluge_effects PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../.. ${CMAKE_CURRENT_LIST_DIR}/../../deluge)
target_compile_features(deluge_effects PUBLIC cxx_std_23)
if(DEFINED DELUGE_CXX_STANDARD)
  set_target_properties(deluge_effects PROPERTIES CXX_STANDARD ${DELUGE_CXX_STANDARD} CXX_STANDARD_REQUIRED ON
                                                  CXX_EXTENSIONS ON)
endif()
target_link_libraries(deluge_effects PUBLIC deluge_plugin_api)
# argon, header-only, as dsp-next links it: the sidechain's public header holds a Control of the duck envelope, and the
# kernel core it reaches includes the NEON vector wrappers. A tree without real NEON gets SIMDe through each consumer:
# tests/support/cmake/specs.cmake's deluge_test_neon() adds it PRIVATE to the target it is called on, so this library
# does not pass it on.
target_link_libraries(deluge_effects PUBLIC argon)
# The filter, the compressor and the sidechain run dsp-next's compiled filter set, compressor and duck envelope
# (deluge_dsp_fixed). That target exists where the app is built (the device and host trees); the unit-test trees build
# no dsp-next library, so their specs compile the sources they run.
# Link-only: dsp-next's compile definitions stay out of this library.
target_link_libraries(deluge_effects PRIVATE $<LINK_ONLY:$<TARGET_NAME_IF_EXISTS:deluge_dsp_fixed>>)
target_compile_options(deluge_effects PUBLIC ${DELUGE_COMMON_COMPILE_OPTIONS})
