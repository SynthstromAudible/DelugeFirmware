# Specs for the services and overlays that link without the app: each compiled here from src/scenes, over the ports'
# fakes (tests/support/scenes), with no legacy song and no host. A source that still reaches the app -- a `host_*` hook,
# the menu scene or the scene registry, or a legacy symbol -- cannot link here and is not listed. The model's host hooks
# (its memory) come from model_hooks_for_tests.cpp.
if(NOT TARGET deluge_scenes_fakes)
  add_subdirectory(${DELUGE_REPO_ROOT}/tests/support/scenes ${CMAKE_CURRENT_BINARY_DIR}/scenes_fakes)
endif()
set(_scenes ${DELUGE_REPO_ROOT}/src/scenes)
deluge_specs_driver(services_fakes_specs TREES host
  SOURCES ${_scenes}/services/midi_learn.cpp
          ${DELUGE_REPO_ROOT}/tests/support/mocks/model_hooks_for_tests.cpp
  LIBRARIES deluge_spec deluge_scenes_bind deluge_scenes_fakes deluge_ui_shell)
