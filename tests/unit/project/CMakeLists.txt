# Specs for the model (src/project), whose library libs.cmake builds against the standalone libraries' include
# roots only, so a hidden dependency on the app fails the build. The model's host hooks come from
# model_hooks_for_tests.cpp: ordinary heap memory and a recording edit guard.
deluge_specs_driver(project_specs TREES host arm
  SOURCES ${DELUGE_REPO_ROOT}/tests/support/mocks/model_hooks_for_tests.cpp
          ${CMAKE_CURRENT_LIST_DIR}/equality/equality.cpp
          ${CMAKE_CURRENT_LIST_DIR}/clean/clean_check.cpp
  LIBRARIES deluge_project deluge_instruments_classic deluge_host_chains deluge_effects
  INCLUDES ${DELUGE_REPO_ROOT}/tests/support/mocks)
# A tree that builds none of these specs defines no project_specs.
if(TARGET project_specs)
  target_compile_definitions(project_specs PRIVATE
    DELUGE_MODEL_RECORDS="${CMAKE_CURRENT_LIST_DIR}/equality/model_records.XML")
endif()
