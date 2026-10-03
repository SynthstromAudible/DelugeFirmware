# Specs for the model (src/deluge-project), added by both the x86 and ARM test trees. The library is built here with
# only the standalone libraries' include roots, so a hidden dependency on the app fails this build. The model's host
# hooks come from model_hooks_for_tests.cpp: ordinary heap memory and a recording edit guard.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/deluge-project ${CMAKE_CURRENT_BINARY_DIR}/model-lib)
deluge_specs_driver(model_specs
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/model_hooks_for_tests.cpp ${CMAKE_CURRENT_LIST_DIR}/../support/mocks/mock_panic.cpp
  LIBRARIES deluge_project
  INCLUDES ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/../support/mocks)
