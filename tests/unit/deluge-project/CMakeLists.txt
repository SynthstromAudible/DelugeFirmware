# Specs for the model (src/deluge-project), whose library libs.cmake builds against the standalone libraries' include
# roots only, so a hidden dependency on the app fails the build. The model's host hooks come from
# model_hooks_for_tests.cpp: ordinary heap memory and a recording edit guard.
deluge_specs_driver(model_specs TREES host arm
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/model_hooks_for_tests.cpp ${CMAKE_CURRENT_LIST_DIR}/equality/equality.cpp
  LIBRARIES deluge_project deluge_instrument deluge_global_effectable deluge_effects
  INCLUDES ${DELUGE_REPO_ROOT}/tests/support/mocks)
# A tree that builds none of these specs defines no model_specs.
if(TARGET model_specs)
  target_compile_definitions(model_specs PRIVATE
    DELUGE_MODEL_RECORDS="${CMAKE_CURRENT_LIST_DIR}/equality/model_records.XML")
endif()
