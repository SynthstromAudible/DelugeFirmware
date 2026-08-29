# deluge_scenes -- the first UI layer allowed to include the model (spec E1, ruling Q4).
# May include scenes/**, shell/**, ui/**, gfx/**, notify/**, libdeluge/**, the standard library,
# and -- being the first model-aware layer -- model/**, modulation/**, playback/**, storage/**,
# gui/l10n/**, definitions_cxx.hpp. Never gui/ otherwise, never hid/.
#
# Link direction (ruling Q4): deluge_scenes links deluge_app; nothing links deluge_scenes until
# spec F needs an entry point -- this task only builds the skeleton and its spec executable.
add_library(deluge_scenes STATIC l10n_bridge.cpp profile.cpp)
target_include_directories(deluge_scenes PUBLIC
    ${SHARED_INCLUDE}
    ${CMAKE_CURRENT_LIST_DIR}/../../include
    ${CMAKE_CURRENT_LIST_DIR}/../deluge
)
# deluge_ui_shell already brings deluge_ui_core/deluge_ui_oled/deluge_ui_grid/deluge_notify PUBLIC
# (src/shell/CMakeLists.txt:11); deluge_app is the portable application (an OBJECT library) this
# layer is allowed to read model/modulation/playback/storage from.
target_link_libraries(deluge_scenes PUBLIC deluge_ui_shell deluge_app)
target_compile_options(deluge_scenes PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})
set_target_properties(deluge_scenes PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES)
