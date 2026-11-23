# The specs held to the device's float flags (-O2 -funsafe-math-optimizations): the float kernels, Poly and Bank, the
# DX7 graph and vector operator, and the FFT's tables and transform, against the DSP library built with the same flags
# (deluge_dsp_fixed_unsafe_math, deluge_dsp_float_unsafe_math). A spec here that #includes a spec from the directory
# above runs that spec again under these flags. A host tree off Linux builds none of them (CMakeLists.txt above).
if(DELUGE_TEST_TREE STREQUAL "host" AND NOT (UNIX AND NOT APPLE))
  return()
endif()
include(${CMAKE_CURRENT_LIST_DIR}/../dsp_specs.cmake)
# No deluge_synth here: it links the plain deluge_dsp, whose objects would sit beside the variants' and the
# archive order would decide which the specs ran.
deluge_specs_driver(dsp_unsafe_math_specs TREES host arm ubsan NEON
  SOURCES ${DELUGE_DSP_SPEC_ASM}
  LIBRARIES deluge_dsp_fixed_unsafe_math deluge_dsp_float_unsafe_math deluge_test_dsp_closure etl::etl
  INCLUDES ${DELUGE_DSP_SPEC_INCLUDES})
if(TARGET dsp_unsafe_math_specs)
  deluge_dsp_spec_flags(dsp_unsafe_math_specs)
  target_compile_options(dsp_unsafe_math_specs PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${DELUGE_TEST_UNSAFE_MATH_OPTIONS}>")
endif()
