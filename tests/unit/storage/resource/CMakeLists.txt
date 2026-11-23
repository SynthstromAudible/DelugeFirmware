# Conformance specs for deluge_storage_resource (src/storage/resource, the Rust cached-asset manager), driven
# through its C ABI (src/storage/resource/include/deluge_storage_resource.h) with a mock Source and linked from the
# deluge_test_rust umbrella staticlib the host tree builds. Its masking discipline calls the interrupt-masking C ABI on
# every acquire and release, so the shared host bodies for it are linked in too.
#
# Linux only: the i686 umbrella staticlib these specs link is built there alone.
if(NOT (UNIX AND NOT APPLE))
  return()
endif()
deluge_specs_driver(resource_specs TREES host
  SOURCES
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_critical_section.cpp
  LIBRARIES
    deluge_rust_rs
    pthread dl m  # the Rust std staticlib's system libraries
  INCLUDES
    ${DELUGE_CRATE_INCLUDE_DIRS}                      # deluge_storage_resource.h
    ${DELUGE_REPO_ROOT}/include                       # libdeluge/alloc.h, libdeluge/system.h
)
