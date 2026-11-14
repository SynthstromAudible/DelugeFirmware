# The src/ libraries the spec directories link, each added once per tree before any spec directory: a directory only
# links, so no directory depends on another having been added first.
#
# A tree's root includes this after specs.cmake (deluge_test_neon) and after its compile options, which the libraries
# are built with.
include_guard(GLOBAL)

set(_deluge_src ${DELUGE_REPO_ROOT}/src)
set(_deluge_libs ${CMAKE_BINARY_DIR}/libs)

# The standalone engine libraries, which the host and the ARM tree both test: serialization, music theory, the plugin
# API and its types, the effects, the global-effectable table, the Deluge instrument and the model. Each is built with
# only the standalone libraries' include roots, so a hidden dependency on the app fails the tree's build.
add_subdirectory(${_deluge_src}/serialization ${_deluge_libs}/serialization)
add_subdirectory(${_deluge_src}/music-theory ${_deluge_libs}/music-theory)
add_subdirectory(${_deluge_src}/plugin-api/types ${_deluge_libs}/plugin-api-types)
add_subdirectory(${_deluge_src}/plugin-api ${_deluge_libs}/plugin-api)
add_subdirectory(${_deluge_src}/effects ${_deluge_libs}/effects)
# The sidechain reaches argon through the kernel core; on a host without NEON that needs SIMDe's backend.
deluge_test_neon(deluge_effects)
add_subdirectory(${_deluge_src}/effects/global_effectable ${_deluge_libs}/global-effectable)
add_subdirectory(${_deluge_src}/instruments/deluge ${_deluge_libs}/instrument)
add_subdirectory(${_deluge_src}/deluge-project ${_deluge_libs}/model)

if(DELUGE_TEST_TREE STREQUAL "host")
  # The host tree's own: printf and MIDI for the app's specs; the scenes' binding to the model; the UI leaf libraries
  # (deluge_gfx_oled / deluge_gfx_grid, deluge_ui_core / deluge_ui_oled / deluge_ui_grid, deluge_ui_shell and
  # deluge_notify), which include from SHARED_INCLUDE; and the level-3 UI script runner (tests/harness/level3).
  cmake_path(SET SHARED_INCLUDE ${_deluge_src})
  add_subdirectory(${_deluge_src}/lib ${_deluge_libs}/printf)
  add_subdirectory(${_deluge_src}/midi ${_deluge_libs}/midi)
  add_subdirectory(${_deluge_src}/scenes/bind ${_deluge_libs}/scenes-bind)
  add_subdirectory(${_deluge_src}/gfx ${_deluge_libs}/gfx)
  add_subdirectory(${_deluge_src}/ui ${_deluge_libs}/ui)
  add_subdirectory(${_deluge_src}/shell ${_deluge_libs}/shell)
  add_subdirectory(${_deluge_src}/notify ${_deluge_libs}/notify)
  add_subdirectory(${DELUGE_REPO_ROOT}/tests/harness/level3 ${_deluge_libs}/level3)
endif()
