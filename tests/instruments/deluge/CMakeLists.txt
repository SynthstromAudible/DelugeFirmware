deluge_specs_driver(instrument_specs
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/../../support/mocks/mock_panic.cpp
  LIBRARIES deluge_instrument
  INCLUDES ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/../../support/mocks)
target_compile_definitions(instrument_specs PRIVATE DELUGE_INSTRUMENT_FIXTURES="${CMAKE_CURRENT_LIST_DIR}/fixtures")
