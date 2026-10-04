deluge_specs_driver(instrument_specs
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/../../support/mocks/mock_panic.cpp
  LIBRARIES deluge_instrument
  INCLUDES ${CMAKE_CURRENT_LIST_DIR}/../../support/mocks)
