# Specs for deluge_scenes_bind (src/scenes/bind): the scenes' binding of menu nodes to the model's records and the
# engines' descriptors. The library links without the app, so these run against a project::Song and fake runtime
# facts, with no engine runtime objects.
if(NOT TARGET deluge_scenes_fakes)
  add_subdirectory(${DELUGE_REPO_ROOT}/tests/support/scenes ${CMAKE_CURRENT_BINARY_DIR}/scenes_fakes)
endif()
deluge_specs_driver(scenes_bind_specs TREES host
  SOURCES ${DELUGE_REPO_ROOT}/tests/support/mocks/model_hooks_for_tests.cpp
          ${CMAKE_CURRENT_LIST_DIR}/support/model_fixture.cpp
          # the name table's definitions; in the app they come from the app's own build of the file
          ${DELUGE_REPO_ROOT}/src/deluge/gui/l10n/setting_names.cpp
  LIBRARIES deluge_scenes_bind deluge_scenes_fakes deluge_host_chains
  INCLUDES ${DELUGE_REPO_ROOT}/tests/support/mocks)
