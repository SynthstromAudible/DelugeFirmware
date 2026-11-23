# The float specs under the fused policy an FMA target runs: -mfma, against dsp-next's float library built with it too
# (deluge_dsp_float_fma). The A9 has no fused multiply-add and an arm64 host rejects -mfma, so only an x86 host builds
# them, and a host tree off Linux none (CMakeLists.txt above). A spec here that #includes a spec from the directory
# above runs that spec again under these flags.
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86"
   OR (DELUGE_TEST_TREE STREQUAL "host" AND NOT (UNIX AND NOT APPLE)))
  return()
endif()
include(${CMAKE_CURRENT_LIST_DIR}/../dsp_specs.cmake)
# No deluge_synth here: it links the plain deluge_dsp, whose objects would sit beside the variant's and the archive
# order would decide which the specs ran.
deluge_specs_driver(dsp_fused_specs TREES host ubsan NEON
  LIBRARIES deluge_dsp_float_fma etl::etl
  INCLUDES ${DELUGE_DSP_SPEC_INCLUDES})
if(TARGET dsp_fused_specs)
  deluge_dsp_spec_flags(dsp_fused_specs)
  target_compile_options(dsp_fused_specs PRIVATE ${DELUGE_TEST_FMA_OPTIONS})
endif()
