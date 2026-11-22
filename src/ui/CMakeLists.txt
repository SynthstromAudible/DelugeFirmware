add_subdirectory(gfx)
add_subdirectory(widgets)
add_subdirectory(controls)
add_subdirectory(shell)
# deluge_ui_scenes (with deluge_ui_scenes_bind): the first UI layer allowed to include the model. It
# compiles against deluge_app, whose usage requirements it takes at generate time. Archived into the
# firmware by the Rust BSP's build script beside deluge_app.
# The test trees build only its binding (tests/support/cmake/libs.cmake): they have no deluge_app to compile against.
if(NOT DEFINED DELUGE_TEST_TREE)
  add_subdirectory(scenes)
endif()
