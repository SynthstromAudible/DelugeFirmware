# Fakes of the scenes' ports (src/ui/scenes/bind/ports), for the unit specs and sim/scenes_spec alike.
add_library(deluge_scenes_fakes STATIC fakes.cpp)
target_include_directories(deluge_scenes_fakes PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(deluge_scenes_fakes PUBLIC deluge_ui_scenes_bind)
