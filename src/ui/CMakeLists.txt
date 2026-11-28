add_subdirectory(gfx)
add_subdirectory(widgets)
add_subdirectory(controls)
add_subdirectory(shell)
add_subdirectory(views/waveform)
add_subdirectory(views/oled)
add_subdirectory(views/grid)
# deluge_ui_scenes (with deluge_ui_scenes_bind): the first UI layer allowed to include the model. It links no part
# of the app, so every tree builds it; the firmware archives it beside deluge_app (the Rust BSP's build script).
add_subdirectory(scenes)
