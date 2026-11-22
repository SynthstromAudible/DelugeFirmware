# Specs for the Deluge instrument (src/instruments/classic), built against the standalone libraries' include roots only,
# so a hidden dependency on the app fails this build.
deluge_specs_driver(instruments_classic_specs TREES host arm
  LIBRARIES deluge_instruments_classic
  INCLUDES ${DELUGE_REPO_ROOT}/tests/support/mocks)
# A tree that builds none of these specs defines no instruments_classic_specs.
if(TARGET instruments_classic_specs)
  target_compile_definitions(instruments_classic_specs PRIVATE DELUGE_INSTRUMENT_FIXTURES="${CMAKE_CURRENT_LIST_DIR}/fixtures")
endif()
