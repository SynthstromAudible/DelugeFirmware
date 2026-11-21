# Specs for the scenes that link without the app: each compiled here from src/scenes, over the ports' fakes
# (tests/support/scenes) and the slicer's fake ops, with no legacy song and no host. A scene that still reaches the
# app -- a `host_*` hook, the menu scene or the scene registry, or a legacy symbol -- cannot link here and is not
# listed.
if(NOT TARGET deluge_scenes_fakes)
  add_subdirectory(${DELUGE_REPO_ROOT}/tests/support/scenes ${CMAKE_CURRENT_BINARY_DIR}/scenes_fakes)
endif()
set(_scenes ${DELUGE_REPO_ROOT}/src/scenes)
deluge_specs_driver(scenes_fakes_specs TREES host
  SOURCES ${_scenes}/slicing/slicer_scene.cpp
          ${_scenes}/services/waveform_paint.cpp
  LIBRARIES deluge_spec deluge_scenes_bind deluge_scenes_fakes deluge_ui_shell deluge_ui_grid deluge_ui_oled
            deluge_notify deluge_gfx_grid deluge_sample_view)
