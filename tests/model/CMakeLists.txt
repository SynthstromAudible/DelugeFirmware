# Specs for the model (src/deluge-project), added by both the x86 and ARM test trees. The model's host hooks come from
# model_hooks_for_tests.cpp: ordinary heap memory and a recording edit guard.
deluge_specs_driver(model_specs
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/model_hooks_for_tests.cpp
  INCLUDES ${CMAKE_CURRENT_LIST_DIR}/../../src ${CMAKE_CURRENT_LIST_DIR})
