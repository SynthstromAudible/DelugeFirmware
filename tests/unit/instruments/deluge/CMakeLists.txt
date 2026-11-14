# Specs for the Deluge instrument (src/instruments/deluge), built against the standalone libraries' include roots only,
# so a hidden dependency on the app fails this build.
deluge_specs_driver(instrument_specs TREES host arm
  SOURCES ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_panic.cpp
  LIBRARIES deluge_instrument
  INCLUDES ${DELUGE_REPO_ROOT}/tests/support/mocks)
target_compile_definitions(instrument_specs PRIVATE DELUGE_INSTRUMENT_FIXTURES="${CMAKE_CURRENT_LIST_DIR}/fixtures")
