#
# deluge_resource (Rust cached-asset manager) conformance specs (CppSpec).
#
# Links the shared deluge_rust umbrella staticlib (built once in ../CMakeLists.txt,
# i686 to match the -m32 test build) and drives the resource manager through its C
# ABI (crates/deluge_resource/include/deluge_resource.h) with a mock Source.
#
# deluge_resource's masking discipline calls the interrupt-masking C ABI on every
# acquire/release, so the shared host bodies for it are linked in too.
#
deluge_specs_driver(resource_specs
  SOURCES
    ../../support/mocks/mock_critical_section.cpp
  LIBRARIES
    deluge_rust_rs
    pthread dl m  # the Rust std staticlib's system libraries
  INCLUDES
    ${CMAKE_CURRENT_LIST_DIR}/../../../crates/deluge_resource/include # deluge_resource.h
    ${CMAKE_CURRENT_LIST_DIR}/../../../include                        # libdeluge/alloc.h, libdeluge/system.h
)
