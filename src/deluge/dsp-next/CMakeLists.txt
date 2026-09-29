# dsp-next — the clean L0 DSP library (synth untangling, Phase 1).
#
# Strangler-fig replacement for src/deluge/dsp/: primitives migrate here one at a
# time, callers switch under golden-master cover, the old impl is deleted. Built
# as two q31/f32 backends behind a header-only numeric-agnostic core.
# See docs/dev/synth_phase1_l0_plan.md.

add_subdirectory(core)

# Configure one numeric backend (fixed/ or float/) as a static library mirroring
# the legacy deluge_dsp target (unity build, no LTO so we can inline, ARM mode on
# hardware). `output` becomes lib<output>.a.
function(deluge_add_dsp_backend target dir output)
    file(GLOB_RECURSE _srcs CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/${dir}/*.c
        ${CMAKE_CURRENT_LIST_DIR}/${dir}/*.cpp
        ${CMAKE_CURRENT_LIST_DIR}/${dir}/*.s
    )
    add_library(${target} STATIC ${_srcs})

    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME ${output}
        C_STANDARD ${DELUGE_C_STANDARD}
        C_STANDARD_REQUIRED ON
        CXX_STANDARD ${DELUGE_CXX_STANDARD}
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
        UNITY_BUILD true
    )

    target_link_libraries(${target} PUBLIC deluge_dsp_core)
    target_link_libraries(${target} PRIVATE argon)

    target_compile_definitions(${target} PUBLIC
        $<$<CONFIG:DEBUG,RELWITHDEBINFO>:IN_HARDWARE_DEBUG=1>
        $<$<CONFIG:DEBUG,RELWITHDEBINFO>:ENABLE_TEXT_OUTPUT=1>
    )

    target_compile_options(${target} PRIVATE
        ${DELUGE_COMMON_COMPILE_OPTIONS}
        # Match deluge_dsp: no LTO (so -finline-functions can do the inlining) and
        # ARM (not Thumb) mode on hardware; -marm is meaningless/omitted off-target.
        "-fno-lto"
        "-finline-functions"
        "$<$<NOT:$<BOOL:${DELUGE_HOST}>>:-marm>"
    )
endfunction()

deluge_add_dsp_backend(deluge_dsp_fixed fixed deluge-dsp-fixed)
deluge_add_dsp_backend(deluge_dsp_float float deluge-dsp-float)

# Umbrella: link deluge_dsp_next to pull in the core interface plus both backends.
add_library(deluge_dsp_next INTERFACE)
target_link_libraries(deluge_dsp_next INTERFACE
    deluge_dsp_core
    deluge_dsp_fixed
    deluge_dsp_float
)
