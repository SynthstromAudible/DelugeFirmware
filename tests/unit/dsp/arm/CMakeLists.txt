# The A9's NEON FM kernel pinned (reference_neon_fm_kernel.s: the device's kernel under a test-local name). Its records
# are what the vector operator's specs hold the Argon operator to on both architectures; the asm exists only on the A9.
include(${CMAKE_CURRENT_LIST_DIR}/../dsp_specs.cmake)
deluge_specs_driver(dsp_arm_specs TREES arm NEON
  SOURCES ${DELUGE_DSP_SPEC_ASM}
  LIBRARIES etl::etl
  INCLUDES ${DELUGE_DSP_SPEC_INCLUDES})
if(TARGET dsp_arm_specs)
  deluge_dsp_spec_flags(dsp_arm_specs)
endif()
