# What the dsp spec drivers share; each directory's CMakeLists.txt includes it.
#
# DELUGE_DSP_SPEC_INCLUDES: this directory, for the shared headers and for the specs a flag-set twin builds again (a
# driver in fused/ or unsafe_math/ includes its own directory, not this one); src/deluge and src, which the specs'
# headers are named from; include/, for the libdeluge headers band_builder_spec reads. Every driver also includes
# tests/ (deluge_specs_driver), for "support/spec_main.h" and "support/golden_update.h".
set(DELUGE_DSP_SPEC_INCLUDES
  ${CMAKE_CURRENT_LIST_DIR}
  ${DELUGE_REPO_ROOT}/src/deluge
  ${DELUGE_REPO_ROOT}/src
  ${DELUGE_REPO_ROOT}/include)

# DELUGE_DSP_SPEC_ASM: on the A9, the device's NEON FM kernel under a test-local name (reference_neon_fm_kernel), which
# the DX7 specs pin and run beside the library's own.
set(DELUGE_DSP_SPEC_ASM)
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
  set(DELUGE_DSP_SPEC_ASM ${CMAKE_CURRENT_LIST_DIR}/reference_neon_fm_kernel.s)
endif()

# deluge_dsp_spec_flags(<driver>): on the A9 the DX7 specs also run the asm (DELUGE_DX7_ASM_PIN).
function(deluge_dsp_spec_flags driver)
  if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
    target_compile_definitions(${driver} PRIVATE DELUGE_DX7_ASM_PIN=1)
  endif()
endfunction()
