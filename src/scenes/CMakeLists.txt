# deluge_scenes -- the first UI layer allowed to include the model (spec E1, ruling Q4).
# May include scenes/**, shell/**, ui/**, gfx/**, notify/**, libdeluge/**, the standard library,
# and -- being the first model-aware layer -- model/**, modulation/**, playback/**, storage/**,
# gui/l10n/**, definitions_cxx.hpp. Never gui/ otherwise, never hid/.
#
# Link direction (ruling Q4): deluge_scenes links deluge_app; nothing links deluge_scenes until
# spec F needs an entry point -- this task only builds the skeleton and its spec executable.
add_library(deluge_scenes STATIC l10n_bridge.cpp profile.cpp menu/node_id.cpp)
target_include_directories(deluge_scenes PUBLIC
    ${SHARED_INCLUDE}
    ${CMAKE_CURRENT_LIST_DIR}/../../include
    ${CMAKE_CURRENT_LIST_DIR}/../deluge
)
# deluge_ui_shell already brings deluge_ui_core/deluge_ui_oled/deluge_ui_grid/deluge_notify PUBLIC
# (src/shell/CMakeLists.txt:11); deluge_app is the portable application (an OBJECT library) this
# layer is allowed to read model/modulation/playback/storage from.
#
# $<COMPILE_ONLY:deluge_app> (CMake >= 3.27 -- both cmake_minimum_required floors that build this
# file were raised to 3.27 for exactly this, see CMakeLists.txt and sim/CMakeLists.txt): deluge_app
# is an OBJECT library, and a real (non-COMPILE_ONLY) PUBLIC link of an OBJECT library into a
# STATIC library archives its ENTIRE object closure into the consumer -- empirically confirmed,
# fix round 1: a real link here made libdeluge_scenes.a 362 archive members / ~70 MB (device) /
# 146 MB (sim), a full second copy of deluge_app, not just this library's own two .cpp files.
# COMPILE_ONLY forwards deluge_app's usage requirements (include dirs, compile definitions) without
# archiving its objects -- exactly what deluge_scenes needs to compile against model/**,
# modulation/**, playback/**, storage/** headers, and nothing more.
#
# Consequence: deluge_scenes no longer provides deluge_app's actual symbols to whatever links it.
# EVERY CONSUMER OF deluge_scenes MUST ALSO LINK deluge_app ITSELF to get those symbols --
# deluge_scenes_spec already does this (sim/scenes_spec/CMakeLists.txt: `deluge_scenes deluge_app
# deluge_scheduler ...`). This is the thing to remember when spec F gives deluge_scenes a real
# entry point: adding `deluge_scenes` to deluge_host's or the firmware device build's link line is
# not enough on its own if that line does not already carry `deluge_app` too (both already do,
# today, for unrelated reasons -- but that is a fact about those link lines, not a guarantee
# `COMPILE_ONLY` provides).
target_link_libraries(deluge_scenes PUBLIC deluge_ui_shell $<COMPILE_ONLY:deluge_app>)
target_compile_options(deluge_scenes PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})
set_target_properties(deluge_scenes PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES)
