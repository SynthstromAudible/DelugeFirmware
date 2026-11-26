# Specs for the services and overlays that link without the app: each compiled here from src/ui/scenes, over the ports'
# fakes (tests/support/ui/scenes), with no legacy song and no host. A source that still reaches the app -- a `host_*`
# hook, the menu scene or the scene registry, or a legacy symbol -- cannot link here and is not listed. The model's host
# hooks (its memory) come from model_hooks_for_tests.cpp. The l10n sources are the strings' definitions, which the app
# builds for itself. NEON: the knob push reads the compressor's settings, whose header reaches the DSP library's kernel core
# and argon.
if(NOT TARGET deluge_ui_scenes_fakes)
  add_subdirectory(${DELUGE_REPO_ROOT}/tests/support/ui/scenes ${CMAKE_CURRENT_BINARY_DIR}/scenes_fakes)
endif()
set(_scenes ${DELUGE_REPO_ROOT}/src/ui/scenes)
set(_l10n ${DELUGE_REPO_ROOT}/src/deluge/gui/l10n)
deluge_specs_driver(services_fakes_specs TREES host NEON
  SOURCES ${_scenes}/services/midi_learn.cpp
          ${_scenes}/services/knob_push.cpp
          ${_scenes}/services/source_names.cpp
          ${_scenes}/overlay/count_in_scene.cpp
          ${_scenes}/overlay/screensaver_scene.cpp
          ${_scenes}/l10n_bridge.cpp
          ${_l10n}/l10n.cpp
          ${_l10n}/g_english.cpp
          ${_l10n}/error_messages.cpp
          ${_l10n}/effect_names.cpp
          ${_l10n}/param_names.cpp
          ${DELUGE_REPO_ROOT}/tests/support/mocks/model_hooks_for_tests.cpp
  LIBRARIES deluge_spec deluge_ui_scenes_bind deluge_ui_scenes_fakes deluge_ui_shell
    deluge_ui_widgets_oled deluge_host_chains)
