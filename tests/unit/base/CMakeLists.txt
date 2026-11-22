# Specs for the dependency-free base library (src/base). function_ref_spec runs on the host only; the division helpers'
# specs run on the ARM tree too, which has neither the spec library nor the MIDI library.
set(_base_spec_libraries "")
if(DELUGE_TEST_TREE STREQUAL "host")
  set(_base_spec_libraries deluge_spec deluge_midi)
endif()
deluge_specs_driver(base_specs TREES host arm ONLY_HOST function_ref_spec.cpp
  LIBRARIES ${_base_spec_libraries}
  INCLUDES ${DELUGE_REPO_ROOT}/src)
