# deluge_scenes -- the first UI layer allowed to include the model (spec E1, ruling Q4).
# May include scenes/**, shell/**, ui/**, gfx/**, notify/**, libdeluge/**, the standard library,
# and -- being the first model-aware layer -- dsp/**, model/**, modulation/**, playback/**,
# processing/**, storage/**, io/**, gui/l10n/**. Never gui/ otherwise, never hid/.
#
# Plus exactly three ROOT-LEVEL app headers, which are named individually because they sit in no
# allowed subdirectory (E2 task 6 fix round 1, Minor 2):
#   definitions_cxx.hpp  -- the shared enums/constants every layer below already reads.
#   deluge.h             -- `Deluge::factoryReset()` only (context_menu/context_menu_scene.cpp).
#                           A `namespace Deluge` declaration plus C boundary externs; it reaches
#                           behind no wall this layer may not cross.
#   version.h            -- `kFirmwareVersionString` (menu/nodes/settings.cpp), a generated header.
#
# THIS LIST IS THE AUTHORITY FOR THOSE THREE, and it is enforced by hand. `check_layer_rules.py`'s
# `scenes-includes-outside` row only matches include paths that contain a `/`, so a root-level
# header -- allowed or not -- is invisible to it; closing that hole needs a stdlib-vs-project
# discriminator (`<cstdint>` and `<stdint.h>` are both slash-less) that the checker does not have.
# A fourth root-level include added here must be justified in this comment or it is a violation
# nothing will catch.
# (io/** joined in task 8, for the settings root: `midiEngine`, `MIDIDeviceManager` and
# `MIDITranspose` -- the state most of the MIDI settings page edits -- live there, and
# check_layer_rules' own NON_UI glob already counts src/deluge/io/ as non-UI.)
# (processing/** is where `Sound` and `Source` live; menu/context.cpp needs their definitions.
# dsp/** joined in task 7, for the one type audio_engine.h only forward declares,
# `deluge::dsp::Reverb`, which menu/relevance.cpp asks `getModel()` of -- see the note on the
# scenes-includes-outside rule in scripts/check_layer_rules.py.)
#
# Link direction (ruling Q4): deluge_scenes links deluge_app; nothing links deluge_scenes until
# spec F needs an entry point -- this task only builds the skeleton and its spec executable.
add_library(deluge_scenes STATIC
    l10n_bridge.cpp
    profile.cpp
    registry.cpp
    menu/bindings.cpp
    menu/context.cpp
    menu/menu_scene.cpp
    menu/node_id.cpp
    menu/relevance.cpp
    menu/shortcuts.cpp
    menu/validate.cpp
    # The menu tables: batch 1 (task 6) is the six subtrees the retired Python generator produced;
    # batch 2 (task 7) is the sound-editor root itself and everything else hanging off it.
    # The `Node` literals themselves live in the matching menu/nodes/sound_*.h headers as
    # `inline constexpr` (fix round 1, I3, so Task 9's constexpr shortcut tables can take their
    # addresses); these translation units hold the get/set hook bodies and the exported root spans.
    # batch 3 (task 8) is the settings root and the nine other roots `SoundEditor::setup` can open.
    menu/nodes/edit_name.cpp
    menu/nodes/roots.cpp
    menu/nodes/settings.cpp
    menu/nodes/sound_actions.cpp
    menu/nodes/sound_arp.cpp
    menu/nodes/sound_bend.cpp
    menu/nodes/sound_compressor.cpp
    menu/nodes/sound_env.cpp
    menu/nodes/sound_filters.cpp
    menu/nodes/sound_fx.cpp
    menu/nodes/sound_lfo.cpp
    menu/nodes/sound_master.cpp
    menu/nodes/sound_osc.cpp
    menu/nodes/sound_randomizer.cpp
    menu/nodes/sound_root.cpp
    menu/nodes/sound_sidechain.cpp
    menu/nodes/sound_unison.cpp
    menu/nodes/sound_voice.cpp
    # E2 task 5: the one scene that replaces legacy's five-class `gui/ui/rename/` directory.
    rename/rename_scene.cpp
    # E2 task 6: the one scene that replaces legacy's `gui::ContextMenu` hierarchy.
    context_menu/context_menu_scene.cpp)
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
