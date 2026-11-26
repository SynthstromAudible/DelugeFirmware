# Specs for the menu's own sources that link without the app: each one compiled here from src/ui/scenes/menu, over the
# ports' fakes (tests/support/ui/scenes), with no legacy song, no host and no other scene. A menu source that still
# reaches the app -- a `host_*` hook, another scene's entry point, or a legacy symbol, directly or through a menu source
# that does -- cannot link here and is not listed. The l10n sources are the strings' definitions, which the app builds
# for itself.
if(NOT TARGET deluge_ui_scenes_fakes)
  add_subdirectory(${DELUGE_REPO_ROOT}/tests/support/ui/scenes ${CMAKE_CURRENT_BINARY_DIR}/scenes_fakes)
endif()
set(_menu ${DELUGE_REPO_ROOT}/src/ui/scenes/menu)
set(_l10n ${DELUGE_REPO_ROOT}/src/deluge/gui/l10n)
deluge_specs_driver(menu_fakes_specs TREES host
  SOURCES ${_menu}/context.cpp
          ${_menu}/note_bridge.cpp
          ${_menu}/nodes/actions.cpp
          ${_menu}/nodes/dx_algorithms.cpp
          ${_menu}/nodes/dx_tables.cpp
          ${_menu}/nodes/dynamic.cpp
          ${_menu}/nodes/midi_command.cpp
          ${_menu}/nodes/midi_device.cpp
          ${DELUGE_REPO_ROOT}/src/ui/scenes/l10n_bridge.cpp
          ${_l10n}/l10n.cpp
          ${_l10n}/g_english.cpp
          ${_l10n}/error_messages.cpp
  LIBRARIES deluge_spec deluge_ui_scenes_bind deluge_ui_scenes_fakes deluge_host_chains)
