if(NOT TARGET deluge_controls)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/controls ${CMAKE_CURRENT_BINARY_DIR}/controls-lib)
endif()
deluge_specs_driver(controls_specs
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/../model/model_hooks_for_tests.cpp ${CMAKE_CURRENT_LIST_DIR}/../support/mocks/mock_panic.cpp
  LIBRARIES deluge_controls deluge_project deluge_instrument deluge_global_effectable
  INCLUDES ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/../support/mocks)
