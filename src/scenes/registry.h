#pragma once

/// @brief The application's scene table -- what `Shell` is constructed with.
///
/// One `SceneRegistry` for the whole application. The shell installs its own
/// `Popup`/`Console`/`Working`/`Loading` instances over whatever the registry reported
/// (`shell/shell.h:133-136`), so an entry in one of those four slots is simply ignored.
///
/// `SceneKind::Screensaver` is the exception among the shell's own kinds, and the one entry here
/// that is not an application kind: the shell owns no screensaver *scene* -- `shell::Screensaver`
/// is a `Service` that pushes the kind by name (`shell/screensaver.cpp:60`) and deliberately does
/// not overwrite the slot (`shell/shell.cpp:177-184`) -- so `overlay::screensaver_scene()` is
/// registered there, by `overlay::kScreensaverSceneKind`. Every other kind runs
/// from `SceneKind::FirstApp` (== 16) upwards: `FirstApp` is the menu interpreter, `FirstApp + 1`
/// the rename editor (`rename::kRenameSceneKind`), `FirstApp + 2` the context menu
/// (`context_menu::kContextMenuSceneKind`), `FirstApp + 3` the file browser
/// (`browser::kBrowserSceneKind`), `FirstApp + 4` the sample marker editor
/// (`marker::kMarkerSceneKind`), `FirstApp + 5` the audio recorder
/// (`recorder::kRecorderSceneKind`), `FirstApp + 6` the mod-encoder value popup
/// (`overlay::kParamPopupSceneKind`), `FirstApp + 7` the record count-in digit
/// (`overlay::kCountInSceneKind`), `FirstApp + 8` the slicer (`slicing::kSlicerSceneKind`),
/// `FirstApp + 9` the audio clip view (`audio_clip::kAudioClipSceneKind`) -- the first ROOT scene
/// in this table.

#include "shell/scene.h"

#include <cstdint>

namespace deluge::scenes {

namespace menu {
class MenuScene;
}

/// @brief Number the application's menu tree, once, and report how many nodes got an id.
///
/// Fix round 2, M1. `menu::assign_node_ids` used to have no caller outside the specs, which left
/// `MenuScene::save_state`, `MenuScene::restore_state` and the shortcut-pad jump each silently
/// no-opping on an unnumbered tree -- three failures that would only surface as "the menu forgets
/// where it was" long after whoever wired the scene up had moved on. Both `menu_scene()` and
/// `scene_registry()` call this, so an application that reaches the menu at all has a numbered
/// tree; it is idempotent (a function-local static) and safe to call again.
///
/// @return The highest id assigned -- i.e. the number of statically reachable nodes. Non-zero for
///         the real tree; a spec that re-numbers a *subset* afterwards (every `tables_*_spec`
///         does) overwrites this, which is the intended behaviour, not a conflict.
uint16_t assigned_node_ids();

/// @brief **Boot wiring this layer needs and does not do for itself -- now done, and here is
///        where.**
///
/// This note used to be a list of three settings that silently stayed at their defaults because
/// nothing called them. **F1 task 8 closed all three**, and the list is replaced by this record
/// rather than deleted: the calls are in `src/deluge`, none of them is anywhere a reader of this
/// layer would look, and the *reason* each lives where it does is the part that has to survive.
///
/// The rule the three share is that none of them is wired from `storage/` -- that would be a
/// storage-depends-on-scenes edge, the wrong direction -- and none of them can be a one-off call at
/// the top of `main` either, because two of the three are settings the user can change while the
/// firmware is running.
///
///   * **`menu::set_shortcuts_version()` (`menu/shortcuts.h`).** The pre-3.0 / current shortcut
///     layout, flash byte 60. Pushed from `SoundEditor::setShortcutsVersion`
///     (`gui/ui/sound_editor.cpp`), which is the sole writer of `SoundEditor::shortcutsVersion` --
///     the `gui/` member this layer may not name and keeps its own copy beside the tables the
///     setting selects -- and which is already called from **all four** places the value can come
///     from, in both layers: `FlashStorage::readSettings` at boot
///     (`storage/flash_storage.cpp:568`, from byte 60), `FlashStorage::resetSettings` (`:311`) --
///     which the *ported* reset menu reaches as well as the legacy one
///     (`scenes/context_menu/context_menu_scene.cpp`'s `accept_reset_flash` / `accept_reset_all`) --
///     the legacy settings item (`gui/menu_item/shortcuts/version.h:27`), and this layer's own
///     `nodes::set_shortcuts_version`, which calls `menu::set_shortcuts_version` directly and does
///     not go through `SoundEditor` at all (see the `@warning` at that hook).
///     Covered by `sim/scenes_spec/boot_wiring_spec.cpp`;
///     the setting's own behaviour is `sim/scenes_spec/tables_3_spec.cpp`'s batch-4 group and
///     `shortcuts_spec.cpp`'s `SHORTCUTS_VERSION_1 layer` group.
///
///   * **`ShellConfig::screensaver_idle_ms` (`shell/shell.h`).** The screensaver's timeout and its
///     off switch, `screensaverMode == ScreensaverMode::OFF ? 0 : screensaverTimeoutMinutes * 60000`
///     -- both live in `FlashStorage`, which `shell::Screensaver` may not read at all
///     (`check_layer_rules.py`'s `shell-includes-outside` row), so the port takes one number and
///     spells "off" as `idle_ms == 0`. Pushed from `Screensaver::settingsChanged()`
///     (`hid/display/screensaver.cpp`), through `Shell::set_screensaver_idle_ms` --
///     which is also the **port of `settingsChanged()` itself** (`shell::Screensaver::set_idle_ms`),
///     the gap `overlay/screensaver_scene.h` recorded. That function is where every writer funnels:
///     `deluge_boot`, the two legacy settings items (`gui/menu_item/defaults/screensaver.h`), and --
///     **closed by F2 task 7** -- the *ported* tree's own two setters (`menu/nodes/settings.cpp`).
///     Those write the two globals directly and still cannot call it, because this layer may not
///     include `hid/`; they file `menu::MenuAction::ScreensaverSettingsChanged` instead
///     (`menu/action.h`) and `gui::ui::dispatchShellActions` makes the call. Until then a change
///     made from the ported screensaver menu was stored but notified neither screensaver until the
///     next boot.
///
///   * **The three services are installed** -- `services::ModKnobs`, `services::GlobalControls`,
///     `services::MidiLearn`. Each is constructed from a `SceneStack&` (the first two also from an
///     `InputLowering&`) and each *uses* it: `ModKnobs::show_popup` pushes
///     `overlay::ParamPopupScene` onto that stack (`services/mod_knobs.cpp:413`) and
///     `ModKnobs::popup_suppressed` (`:351`) and `ModKnobs::mod_encoder_button_action` (`:564`)
///     read `stack_.input_top()`; `GlobalControls::handle` gates its whole `View::buttonAction`
///     group on `stack_.input_top().is_root()` (`services/global_controls.cpp:580`);
///     `MidiLearn::handle` reads the same (`services/midi_learn.cpp:491`). The only `SceneStack`
///     and `InputLowering` in an application are the ones the **`Shell` owns**, so a real service
///     cannot be built before the shell it serves, and until F1 task 2's `Shell::set_services` it
///     could not be installed after it either -- `Services` was passed by value into the
///     constructor and fixed there. `gui/ui/shell_guest_services.cpp` is the application half:
///     it owns the three, seats them over `shellHostUI`'s own shell, and re-seeds what they borrow
///     from legacy (`currentSong`, `View`'s active mod controllable and mod region, and the two
///     `ViewState` blocks) at boot and on every guest open. E4 task 13 found the gap;
///     `sim/scenes_spec/scene_scripts_spec.cpp` is the level-2 demonstration of the setter and
///     `sim/scenes_spec/boot_wiring_spec.cpp` asserts the real installation.
///
/// Nothing else in `src/scenes` is in this position: every other setting the menu tree edits is read
/// and written through the same `FlashStorage` / `runtimeFeatureSettings` / `midiEngine` global the
/// existing loader already populates.

/// @brief The application's one menu interpreter.
///
/// A function-local static rather than a namespace-scope object: it must outlive every `Shell`
/// that borrows it and must not participate in static-initialisation order across translation
/// units.
/// @return The singleton.
menu::MenuScene& menu_scene();

// (No `rename_scene()` forwarder here, deliberately. `rename::rename_scene()` already is the
// singleton accessor, and a same-named function in this namespace would be ambiguous with it for
// every consumer that writes `using namespace deluge::scenes;` -- which every spec in this tree
// does. `scene_registry()` below calls the real one.)

/// @brief The application's scene table, viewing storage this library owns.
///
/// @return A registry whose `by_kind[SceneKind::FirstApp]` is `&menu_scene()`, whose
///         `by_kind[SceneKind::FirstApp + 1]` is `&rename_scene()`, whose
///         `by_kind[SceneKind::FirstApp + 2]` is `&context_menu_scene()`, whose
///         `by_kind[SceneKind::FirstApp + 3]` is `&browser_scene()`, whose
///         `by_kind[SceneKind::FirstApp + 4]` is `&marker_scene()`, whose
///         `by_kind[SceneKind::FirstApp + 5]` is `&recorder_scene()`, whose
///         `by_kind[SceneKind::FirstApp + 6]` is `&overlay::param_popup_scene()`, whose
///         `by_kind[SceneKind::FirstApp + 7]` is `&overlay::count_in_scene()`, whose
///         `by_kind[SceneKind::FirstApp + 8]` is `&slicing::slicer_scene()`, whose
///         `by_kind[SceneKind::FirstApp + 9]` is `&audio_clip::audio_clip_scene()`, whose
///         `by_kind[SceneKind::Screensaver]` is `&overlay::screensaver_scene()` (the one
///         shell-owned kind an application fills in -- see the note above), and whose every other
///         slot is null.
deluge::ui::shell::SceneRegistry scene_registry();

} // namespace deluge::scenes
