#
# deluge_alloc (Rust TLSF core) conformance specs (CppSpec).
#
# Links the shared deluge_rust umbrella staticlib (built once in ../CMakeLists.txt,
# i686 to match the -m32 test build + the 32-bit RZ/A1L pointer width) and runs the
# shared conformance suite (../memory) against the Rust allocator through its
# C ABI (include/libdeluge/alloc.h).
#
deluge_specs_driver(alloc_specs
  LIBRARIES
    deluge_rust_rs
    pthread dl m  # the Rust std staticlib's system libraries
  INCLUDES
    ${CMAKE_CURRENT_LIST_DIR}/../memory            # conformance.h, allocator_under_test.h
    ${CMAKE_CURRENT_LIST_DIR}/../../../include     # libdeluge/alloc.h
)
