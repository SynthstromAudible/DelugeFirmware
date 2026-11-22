#pragma once

/// @brief The application's scene table -- what `Shell` is constructed with.
///
/// One `SceneRegistry` for the whole application. The shell installs its own
/// `Popup`/`Console`/`Working`/`Loading`/`Notification` instances over whatever the registry reported
/// (the `Shell` constructor), so an entry in one of those five slots is simply ignored.
///
/// `SceneKind::Screensaver` is the exception among the shell's own kinds, and the one entry here
/// that is not an application kind: the shell owns no screensaver *scene* -- `shell::Screensaver`
/// is a `Service` that pushes the kind by name (`Screensaver::tick`) and deliberately does
/// not overwrite the slot (the `Shell` constructor) -- so `overlay::screensaver_scene()` is
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

#include "ui/shell/scene.h"

#include <cstdint>

namespace deluge::ui::scenes {

namespace menu {
class MenuScene;
}

/// @brief Number the application's menu tree, once, and report how many nodes got an id.
///
/// `MenuScene::save_state`, `MenuScene::restore_state` and the shortcut-pad jump each silently
/// no-op on an unnumbered tree -- failures that would only surface as "the menu forgets where it
/// was". Both `menu_scene()` and `scene_registry()` call this, so an application that reaches the
/// menu at all has a numbered tree; it is idempotent (a function-local static) and safe to call again.
///
/// @return The highest id assigned -- i.e. the number of statically reachable nodes. Non-zero for
///         the real tree; a spec that re-numbers a *subset* afterwards (every `tables_*_spec`
///         does) overwrites this, which is the intended behaviour, not a conflict.
uint16_t assigned_node_ids();

/// @brief **Boot wiring this layer needs and does not do for itself, and where it is done.**
///
/// Three settings would silently stay at their defaults if nothing called them. The calls are in
/// `src/deluge`, none of them anywhere a reader of this layer would look, and the *reason* each
/// lives where it does is the part that has to survive.
///
/// The rule the three share is that none of them is wired from `storage/` -- that would be a
/// storage-depends-on-scenes edge, the wrong direction -- and none of them can be a one-off call at
/// the top of `main` either, because two of the three are settings the user can change while the
/// firmware is running.
///
///   * **`menu::shortcuts_version(ctx)` (`menu/shortcuts.h`).** The pre-3.0 / current shortcut layout, the device's
///     `pads/shortcutsVersion` row, which this layer reads through the device port where it needs it, so no push is
///     wired. The host keeps it in flash byte 60 (`FlashStorage::shortcutsVersion`): `FlashStorage::readSettings`
///     fills it at boot, `FlashStorage::resetSettings` resets it (the reset menu item reaches that) and the settings
///     item writes the row through the device port. Covered by
///     `tests/integration/ui/scenes/boot_wiring_spec.cpp`; the setting's own behaviour is
///     `tests/integration/ui/scenes/tables_3_spec.cpp`'s `tables_4_items` group and `shortcuts_spec.cpp`'s
///     `SHORTCUTS_VERSION_1 layer` group.
///
///   * **`ShellConfig::screensaver_idle_ms` (`ui/shell/shell.h`).** The screensaver's timeout and its off switch,
///     `screensaverMode == ScreensaverMode::OFF ? 0 : screensaverTimeoutMinutes *
///     60000` -- both live in `FlashStorage`, which `shell::Screensaver` may not read at all (`check_layer_rules.py`'s
///     `shell-includes-outside` row), so the port takes one number and spells "off" as `idle_ms == 0`. Pushed from
///     `Screensaver::settingsChanged()` (`hid/display/screensaver.cpp`), through `Shell::set_screensaver_idle_ms` --
///     which is also the **port of `settingsChanged()` itself** (`shell::Screensaver::set_idle_ms`), the gap
///     `overlay/screensaver_scene.h` records. That function is where every writer funnels: `deluge_boot`, the two
///     legacy settings items (`origin/main:src/deluge/gui/menu_item/defaults/screensaver.h`), and the device port's
///     store for the `ui/screensaver/mode` and `ui/screensaver/timeout` rows, which the ported tree's two items write;
///     this layer may not include `hid/`, so the host makes the call at the write.
///
///   * **The three services are installed** -- `services::ModKnobs`, `services::GlobalControls`,
///     `services::MidiLearn`. Each is constructed from a `SceneStack&` (the first two also from an
///     `InputLowering&`) and each *uses* it: its `GuestKnobPresentation` pushes
///     `overlay::ParamPopupScene` onto that stack (`GuestKnobPresentation::show_value_popup`), and
///     `GuestKnobPresentation::suppress_value_popup` and `ModKnobs::push`
///     read `stack_.input_top()`; `GlobalControls::handle` gates its whole `View::buttonAction`
///     group on `stack_.input_top().is_root()` (`GlobalControls::handle`);
///     `MidiLearn::handle` reads the same. The only `SceneStack`
///     and `InputLowering` in an application are the ones the **`Shell` owns**, so a real service
///     cannot be built before the shell it serves; `Shell::set_services` installs them after it is
///     constructed. `gui/ui/shell_guest_services.cpp` is the application half:
///     it owns the three, seats them over `shellHostUI`'s own shell, and re-seeds what they borrow
///     from legacy (`currentSong`, `View`'s active mod controllable and mod region, and the two
///     `ViewState` blocks) at boot and on every guest open.
///     `tests/integration/ui/scenes/scene_scripts_spec.cpp` is the level-2 demonstration of the setter and
///     `tests/integration/ui/scenes/boot_wiring_spec.cpp` asserts the real installation.
///
/// Nothing else in `src/ui/scenes` is in this position: every other setting the menu tree edits is read
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
// every consumer that writes `using namespace deluge::ui::scenes;` -- which every spec in `tests/integration/ui/scenes`
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

} // namespace deluge::ui::scenes
