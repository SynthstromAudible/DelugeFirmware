deluge_specs_driver(global_effectable_specs
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/../../support/mocks/mock_panic.cpp
  LIBRARIES deluge_global_effectable deluge_instrument
  INCLUDES ${CMAKE_CURRENT_LIST_DIR}/../../support/mocks)
