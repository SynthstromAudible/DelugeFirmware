# Specs for deluge_scenes_bind: the scenes' binding of menu nodes to the model's records and the engines' descriptors.
# The library links without the app, so these run against a project::Song and fake runtime facts, with no engine
# runtime objects. Added after tests/model, which builds deluge_project and the engine libraries.
if(NOT TARGET deluge_scenes_bind)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/scenes/bind ${CMAKE_CURRENT_BINARY_DIR}/scenes-bind-lib)
endif()
deluge_specs_driver(scenes_bind_specs
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/../model/model_hooks_for_tests.cpp ${CMAKE_CURRENT_LIST_DIR}/../support/mocks/mock_panic.cpp
          ${CMAKE_CURRENT_LIST_DIR}/support/model_fixture.cpp
          # the name table's definitions; in the app they come from the app's own build of the file
          ${CMAKE_CURRENT_LIST_DIR}/../../src/deluge/gui/l10n/setting_names.cpp
  LIBRARIES deluge_scenes_bind deluge_global_effectable
  INCLUDES ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/../model ${CMAKE_CURRENT_LIST_DIR}/../support/mocks)
