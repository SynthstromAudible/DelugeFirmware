#
# deluge_alloc (Rust TLSF core) conformance specs (CppSpec).
#
# Builds the Rust crate as a 32-bit (i686) staticlib via cargo and links it into
# a CppSpec driver that runs the shared conformance suite (../spec_memory) against
# the Rust allocator through its C ABI (include/libdeluge/alloc.h). i686 matches
# the -m32 test build (and the 32-bit RZ/A1L device pointer width), so no separate
# 64-bit target is needed.
#
include(FetchContent)
FetchContent_Declare(CppSpec
  URL https://github.com/toroidal-code/cppspec/archive/refs/heads/main.tar.gz
)
FetchContent_MakeAvailable(CppSpec)

set(ALLOC_CRATE ${CMAKE_CURRENT_LIST_DIR}/../../crates/deluge_alloc)
set(ALLOC_TARGET i686-unknown-linux-gnu)
set(ALLOC_LIB ${ALLOC_CRATE}/target/${ALLOC_TARGET}/release/libdeluge_alloc.a)

# Build (or rebuild, when the Rust sources change) the staticlib via cargo.
add_custom_command(
  OUTPUT ${ALLOC_LIB}
  COMMAND cargo build --release --target ${ALLOC_TARGET}
  WORKING_DIRECTORY ${ALLOC_CRATE}
  DEPENDS
    ${ALLOC_CRATE}/src/lib.rs
    ${ALLOC_CRATE}/src/tlsf.rs
    ${ALLOC_CRATE}/Cargo.toml
  COMMENT "Building Rust deluge_alloc staticlib (${ALLOC_TARGET})"
  VERBATIM
)
add_custom_target(deluge_alloc_rs_build DEPENDS ${ALLOC_LIB})

add_library(deluge_alloc_rs STATIC IMPORTED GLOBAL)
set_target_properties(deluge_alloc_rs PROPERTIES IMPORTED_LOCATION ${ALLOC_LIB})
add_dependencies(deluge_alloc_rs deluge_alloc_rs_build)

function(create_specs_driver driver_name spec_dir)
  file(GLOB_RECURSE spec_sources RELATIVE ${spec_dir} ${spec_dir}/*_spec.cpp)
  create_test_sourcelist(specs ${driver_name}.cpp ${spec_sources})
  add_executable(${driver_name} ${specs})
  target_link_libraries(${driver_name} PRIVATE c++spec deluge_alloc_rs
    # System libs the Rust std staticlib references.
    pthread dl m)
  target_include_directories(${driver_name} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../spec_memory  # conformance.h, allocator_under_test.h
    ${CMAKE_CURRENT_LIST_DIR}/../../include   # libdeluge/alloc.h
  )
  set_target_properties(${driver_name} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS ON
  )
  foreach(spec IN LISTS spec_sources)
    cmake_path(GET spec STEM LAST_ONLY spec_name)
    add_test(NAME ${spec_name} COMMAND ${driver_name} ${spec_name} --verbose)
  endforeach()
endfunction()

create_specs_driver(alloc_specs ${CMAKE_CURRENT_LIST_DIR})
