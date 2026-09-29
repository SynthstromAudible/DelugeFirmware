# libdeluge-dsp core — numeric-agnostic interfaces, std::span block types, strong
# numeric aliases, and q31<->f32 conversion. Header-only, so an INTERFACE target.
# See docs/dev/synth_phase1_l0_plan.md.
add_library(deluge_dsp_core INTERFACE)

target_include_directories(deluge_dsp_core INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/../..   # src/deluge : so #include "dsp-next/core/..." and "util/..." resolve
    ${SHARED_INCLUDE}                 # src        : so #include "deluge/..." resolves
)

# argon's usage requirements (the NEON/SIMDe vector wrappers) propagate to every
# backend that links core.
target_link_libraries(deluge_dsp_core INTERFACE argon)
