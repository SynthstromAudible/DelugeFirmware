# deluge_scenes -- the first UI layer allowed to include the model.
# May include scenes/**, shell/**, ui/**, gfx/**, notify/**, libdeluge/**, the standard library,
# and -- being the first model-aware layer -- model/**, modulation/**, playback/**,
# processing/**, storage/**, io/**, gui/l10n/**, and the model's words and values: engines/**,
# plugin-api/**, serialization/**, sequence/**, and the model's records: deluge-project/** (scenes/bind binds menu nodes
# to them). Never gui/ otherwise, never hid/.
#
# Plus exactly three ROOT-LEVEL app headers, which are named individually because they sit in no
# allowed subdirectory:
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
# (io/** is for the settings root: `midiEngine`, `MIDIDeviceManager` and
# `MIDITranspose` -- the state most of the MIDI settings page edits -- live there, and
# check_layer_rules' own NON_UI glob already counts src/deluge/io/ as non-UI.)
# (processing/** is where `Sound` and `Source` live; menu/context.cpp needs their definitions.)
#
# Link direction: deluge_scenes links deluge_app (compile-only; see below), never the reverse.
add_library(deluge_scenes STATIC
    l10n_bridge.cpp
    profile.cpp
    registry.cpp
    menu/bindings.cpp
    menu/context.cpp
    menu/chains.cpp
    menu/learn.cpp
    menu/menu_core.cpp
    menu/menu_scene.cpp
    menu/present.cpp
    menu/shortcut_overlay.cpp
    menu/value_session.cpp
    menu/view.cpp
    menu/voice_usage.cpp
    menu/walk.cpp
    menu/node_id.cpp
    menu/note_bridge.cpp
    menu/page_buttons.cpp
    menu/pads.cpp
    menu/relevance.cpp
    menu/shortcut_blink.cpp
    menu/shortcuts.cpp
    menu/validate.cpp
    # The menu tables: the sound editor's six subtrees, the sound-editor root and everything hanging
    # off it, the settings root and the nine other roots `SoundEditor::setup` can open. The `Node`
    # literals live in the matching menu/nodes/*.h headers as `inline constexpr` (so the constexpr
    # shortcut tables can take their addresses); these translation units hold the get/set hook
    # bodies and the exported root spans.
    menu/nodes/actions.cpp
    menu/nodes/column_hooks.cpp
    menu/nodes/context_menu_items.cpp
    menu/nodes/dx_tables.cpp
    menu/nodes/dynamic.cpp
    menu/nodes/edit_name.cpp
    menu/nodes/midi_command.cpp
    menu/nodes/midi_device.cpp
    menu/nodes/multi_range.cpp
    menu/nodes/note.cpp
    menu/nodes/output_items.cpp
    menu/nodes/patch_cables.cpp
    menu/nodes/roots.cpp
    menu/nodes/sample.cpp
    menu/nodes/scales.cpp
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
    # the one scene that replaces legacy's five-class `gui/ui/rename/` directory.
    rename/rename_scene.cpp
    # the one scene that replaces legacy's `gui::ContextMenu` hierarchy.
    context_menu/context_menu_scene.cpp
    # the async listing seam. Not a scene -- a request/poll object a scene owns, sitting
    # between the tick-driven shell and `storage::Owner`/`storage::FileCatalogue`.
    browser/listing.cpp
    # the one scene that replaces the two `gui/ui/browser/` subclasses the menu tree
    # reaches -- `SampleBrowser` and `LoadMidiDeviceDefinitionUI` -- over the seam above.
    browser/browser_scene.cpp
    # the sample marker editor.
    marker/marker_scene.cpp
    # the audio recorder -- the one scene that replaces `gui/ui/audio_recorder.cpp`'s
    # menu-driven half (its output-recording half stays on the legacy object; see the header).
    recorder/recorder_scene.cpp
    # the mod-encoder value popup -- the first of the three overlay scenes.
    overlay/param_popup_scene.cpp
    # the record count-in digit -- the second of the three overlay scenes. Not a port of
    # a `View`/UI method; the popup lifecycle lives in `gui/ui/notify_drain.cpp`'s drain, and the
    # count itself moves in `playback::PlaybackHandler`. See `overlay/count_in_scene.h`.
    overlay/count_in_scene.cpp
    # the idle animation -- the third overlay scene, and the only one registered under a
    # kind of the SHELL's own (`SceneKind::Screensaver`), because `shell::Screensaver` pushes it by
    # name. The two animations it draws moved out of `hid/display/` to `ui/oled/` in the same task.
    overlay/screensaver_scene.cpp
    # the gold-knob half of legacy's `View`, as a `shell::Service` that lives here
    # (not in `src/shell`) because it is entirely about the model. See `services/mod_knobs.h`.
    services/mod_knobs.cpp
    # What a gold knob's push and a mod button's press do, over the song's records. See `services/knob_push.h`.
    services/knob_push.cpp
    # A patch source's short and full names, which the popup, the mod matrix and the push all show.
    services/source_names.cpp
    # A sample's full-screen waveform on the main pads, for the marker editor, the slicer and the sample browser.
    services/waveform_paint.cpp
    # the transport-and-global half of legacy's `View`, plus the tail of
    # `Buttons::buttonAction`. Here for the same reason `mod_knobs.cpp` is. See
    # `services/global_controls.h`.
    services/global_controls.cpp
    # the MIDI-learn half of legacy's `View` -- the mode, the held thing, the learn
    # commit and the flash phase. Here for the same reason the other two services are: learning
    # writes the model. See `services/midi_learn.h`.
    services/midi_learn.cpp
    # the horizontal scroll and zoom every clip view shares -- the state and pure
    # arithmetic half of legacy's `TimelineView` -> `ClipNavigationTimelineView` -> `ClipView`
    # chain. Not a scene: a plain struct the root scenes own. See `timeline/timeline.h` for the
    # symbol-by-symbol split between this file, the scene, and the dropped `PadLEDs` animation.
    timeline/timeline.cpp
    # the slicer -- state, render and the three encoder arms. NOT a root scene: legacy's
    # `Slicer` is a plain `UI` opened over the sample browser. See `slicing/slicer_scene.h`.
    slicing/slicer_scene.cpp
    # the audio clip view -- render and navigation. The FIRST root scene in the tree
    # (`AudioClipView` -> `ClipView` -> `ClipNavigationTimelineView` -> `TimelineView : public
    # RootUI`), and therefore the first for which `Scene::is_root()` is true. See
    # `audio_clip/audio_clip_scene.h`.
    audio_clip/audio_clip_scene.cpp)
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
# STATIC library archives its ENTIRE object closure into the consumer: a real link here makes
# libdeluge_scenes.a a full second copy of deluge_app (~70 MB device, ~146 MB sim).
# COMPILE_ONLY forwards deluge_app's usage requirements (include dirs, compile definitions) without
# archiving its objects -- exactly what deluge_scenes needs to compile against model/**,
# modulation/**, playback/**, storage/** headers, and nothing more.
#
# Consequence: deluge_scenes no longer provides deluge_app's actual symbols to whatever links it.
# EVERY CONSUMER OF deluge_scenes MUST ALSO LINK deluge_app ITSELF to get those symbols --
# deluge_scenes_spec already does this (sim/scenes_spec/CMakeLists.txt: `deluge_scenes deluge_app
# deluge_scheduler ...`), and so do deluge_host's and the firmware device build's link lines --
# a fact about those link lines, not a guarantee `COMPILE_ONLY` provides.
# deluge_scenes_bind: what a menu node edits, bound to the model's records and the engines' descriptors.
if(NOT TARGET deluge_scenes_bind)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/bind ${CMAKE_CURRENT_BINARY_DIR}/bind)
endif()
target_link_libraries(deluge_scenes PUBLIC deluge_ui_shell deluge_scenes_bind $<COMPILE_ONLY:deluge_app>)
target_compile_options(deluge_scenes PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})
set_target_properties(deluge_scenes PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES)
# UI code, run at menu and input rate: built for size, as deluge_scenes_bind is.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/optimise_for_size.cmake)
deluge_optimise_target_for_size(deluge_scenes)
