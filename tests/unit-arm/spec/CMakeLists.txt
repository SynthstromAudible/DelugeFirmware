include(FetchContent)
FetchContent_Declare(CppSpec
  URL https://github.com/toroidal-code/cppspec/archive/refs/heads/main.tar.gz
)
FetchContent_MakeAvailable(CppSpec)

# Pinned identically to lib/CMakeLists.txt. On this ARM target the toolchain provides a real
# <arm_neon.h> (-mfpu=neon, __ARM_NEON defined), so argon's arm_simd.hpp dispatch picks the native
# NEON backend directly -- no SIMDe, no compat shim.
FetchContent_Declare(argon
  GIT_REPOSITORY https://github.com/stellar-aria/argon
  GIT_TAG 5897db725c1cdee452b5f288614796364468f2ea
)
FetchContent_MakeAvailable(argon)

add_library(deluge_spec INTERFACE)

target_include_directories(deluge_spec INTERFACE
  ../../../src/deluge
  ../../../src
  ../../32bit_unit_test/mocks/
)
target_link_libraries(deluge_spec INTERFACE argon)

set_target_properties(deluge_spec PROPERTIES
  CXX_STANDARD 26
  CXX_STANDARD_REQUIRED YES
)

function(create_specs_driver driver_name spec_dir)
  # Discover Specs
  file(GLOB_RECURSE spec_sources RELATIVE ${spec_dir} ${spec_dir}/*_spec.cpp)

  create_test_sourcelist(specs ${driver_name}.cpp ${spec_sources})
  add_executable(${driver_name} ${specs})
  target_link_libraries(${driver_name} PRIVATE c++spec)
  set_target_properties(${driver_name} PROPERTIES
    CXX_STANDARD 26
    CXX_STANDARD_REQUIRED YES
  )

  foreach(spec IN LISTS spec_sources)
    cmake_path(GET spec STEM LAST_ONLY spec_name)
    add_test(
      NAME ${spec_name}
      COMMAND ${driver_name} ${spec_name} --verbose
    )
  endforeach()
endfunction()

create_specs_driver(all_specs ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(all_specs PUBLIC deluge_spec)
