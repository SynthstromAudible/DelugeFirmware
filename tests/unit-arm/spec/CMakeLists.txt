# On this ARM target the toolchain provides a real <arm_neon.h> (-mfpu=neon, __ARM_NEON defined), so argon's
# arm_simd.hpp dispatch picks the native NEON backend directly -- no SIMDe, no compat shim.

add_library(deluge_spec INTERFACE)

target_include_directories(deluge_spec INTERFACE
  ../../../src/deluge
  ../../../src
)
target_link_libraries(deluge_spec INTERFACE argon)

set_target_properties(deluge_spec PROPERTIES
  CXX_STANDARD 26
  CXX_STANDARD_REQUIRED YES
)

deluge_specs_driver(all_specs)
target_link_libraries(all_specs PUBLIC deluge_spec)
