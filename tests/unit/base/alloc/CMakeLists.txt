# Conformance specs for deluge_base_alloc (src/base/alloc, the Rust TLSF core) through its C ABI
# (src/base/alloc/include/deluge_base_alloc.h): the allocator-agnostic conformance suite (tests/support/fixtures/memory:
# conformance.h, allocator_under_test.h) against the Rust allocator, linked from the deluge_test_rust umbrella
# staticlib the host tree builds, i686 to match the -m32 test build and the RZ/A1L's 32-bit pointers.
#
# Linux only: the i686 umbrella staticlib these specs link is built there alone.
if(NOT (UNIX AND NOT APPLE))
  return()
endif()
deluge_specs_driver(alloc_specs TREES host
  LIBRARIES
    deluge_rust_rs
    pthread dl m  # the Rust std staticlib's system libraries
  INCLUDES
    ${DELUGE_REPO_ROOT}/tests/support/fixtures/memory  # conformance.h, allocator_under_test.h
    ${DELUGE_REPO_ROOT}/include                        # libdeluge/types.h, libdeluge/abi.h
    ${DELUGE_CRATE_INCLUDE_DIRS}                       # the crates' C headers
)
