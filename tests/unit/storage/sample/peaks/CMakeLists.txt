# Specs for the peak index's C ABI (src/storage/sample/peaks), driven from C++ as the app calls it. They link the
# sample_abi build of the Rust umbrella (deluge_rust_rs_sim), which builds the crate with c_abi and test_support.
#
# Linux only: the i686 umbrella staticlib these specs link is built there alone.
if(NOT (UNIX AND NOT APPLE))
  return()
endif()
deluge_specs_driver(peaks_specs TREES host
  SOURCES
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_critical_section.cpp
  LIBRARIES
    deluge_rust_rs_sim
    pthread dl m  # the Rust std staticlib's system libraries
  INCLUDES
    ${DELUGE_CRATE_INCLUDE_DIRS}                      # the crates' C headers
    ${DELUGE_REPO_ROOT}/include                       # libdeluge/abi.h
)
