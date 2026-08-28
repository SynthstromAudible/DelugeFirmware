# deluge_ui_shell — surfaces, scene stack, input lowering, services, tick, flush (spec D).
# May include shell/**, ui/**, gfx/**, notify/**, libdeluge/** and the standard library only.
add_library(deluge_ui_shell STATIC profile.cpp surfaces.cpp board_surfaces.cpp scene_stack.cpp input_lowering.cpp router.cpp notices.cpp leds.cpp shell.cpp screensaver.cpp boot_scenes.cpp)
target_include_directories(deluge_ui_shell PUBLIC ${SHARED_INCLUDE})
# <libdeluge/...> boundary headers. Spelled relative to this file (not ${CMAKE_SOURCE_DIR}/include,
# which only resolves to the repo's include/ when this is built from the top-level project --
# the tests/ and sim/ projects each have their own, different CMAKE_SOURCE_DIR). Mirrors
# tests/spec_alloc/CMakeLists.txt and its siblings, which need the same headers from inside the
# tests project.
target_include_directories(deluge_ui_shell PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(deluge_ui_shell PUBLIC deluge_ui_core deluge_ui_oled deluge_ui_grid deluge_notify)
target_compile_options(deluge_ui_shell PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})
set_target_properties(deluge_ui_shell PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES)
