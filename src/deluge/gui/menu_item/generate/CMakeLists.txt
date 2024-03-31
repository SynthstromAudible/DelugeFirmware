#
# Build menus
#

file(GLOB_RECURSE deluge_menu_gen_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.py
)

set(deluge_menus_inc ${CMAKE_CURRENT_LIST_DIR}/g_menus.inc)

add_custom_command(
  OUTPUT  ${deluge_menus_inc}
  COMMAND
    ${Python_EXECUTABLE}
  ARGS
    ${CMAKE_CURRENT_LIST_DIR}/main.py
    -c ${deluge_menus_inc}
  DEPENDS ${deluge_menu_gen_SOURCES}
  VERBATIM
)

add_custom_target(deluge_menus DEPENDS ${deluge_menus_inc})

