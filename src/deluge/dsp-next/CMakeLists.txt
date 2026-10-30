# dsp-next — the clean L0 DSP library (synth untangling, Phase 1).
#
# Strangler-fig replacement for src/deluge/dsp/: primitives migrate here one at a
# time, callers switch under golden-master cover, the old impl is deleted. Built
# as two q31/f32 backends behind a header-only numeric-agnostic core.
# See docs/dev/synth_phase1_l0_plan.md.

add_subdirectory(core)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/deluge_library.cmake)

# Configure one numeric backend (fixed/ or float/) as a static library built with the library flags (unity build, no
# LTO so we can inline, ARM mode on hardware). `output` becomes lib<output>.a.
function(deluge_add_dsp_backend target dir output)
    file(GLOB_RECURSE _srcs CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/${dir}/*.c
        ${CMAKE_CURRENT_LIST_DIR}/${dir}/*.cpp
        ${CMAKE_CURRENT_LIST_DIR}/${dir}/*.s
    )
    add_library(${target} STATIC ${_srcs})
    set_target_properties(${target} PROPERTIES OUTPUT_NAME ${output})
    deluge_library_target(${target})
    target_link_libraries(${target} PUBLIC deluge_dsp_core)
    target_link_libraries(${target} PRIVATE argon)
endfunction()

deluge_add_dsp_backend(deluge_dsp_fixed fixed deluge-dsp-fixed)
# The firmware renders the DX7's modern no-input and bus-input ops through its NEON kernel (fixed/dx7/neon_fm_kernel.s,
# assembled only where the ASM language is enabled: the device build); every other build steps the Argon operator the
# kernel is pinned to.
if(NOT DELUGE_HOST)
    target_compile_definitions(deluge_dsp_fixed PRIVATE DELUGE_DX7_NEON_KERNEL=neon_fm_kernel)
endif()
deluge_add_dsp_backend(deluge_dsp_float float deluge-dsp-float)

# Umbrella: link deluge_dsp_next to pull in the core interface plus both backends.
add_library(deluge_dsp_next INTERFACE)
target_link_libraries(deluge_dsp_next INTERFACE
    deluge_dsp_core
    deluge_dsp_fixed
    deluge_dsp_float
)
