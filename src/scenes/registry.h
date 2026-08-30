#pragma once

/// @brief The application's scene table -- what `Shell` is constructed with.
///
/// One `SceneRegistry` for the whole application, holding **application scenes only**. The shell
/// installs its own `Popup`/`Console`/`Working`/`Boot`/`Loading` instances over whatever the
/// registry reported (`shell/shell.h:120-126,330-342`), and `SceneKind::Screensaver` names no
/// shell-owned scene at all, so an entry in any of those slots would simply be ignored. Kinds run
/// from `SceneKind::FirstApp` (== 16) upwards: `FirstApp` is the menu interpreter, `FirstApp + 1`
/// the rename editor (`rename::kRenameSceneKind`), `FirstApp + 2` the context menu
/// (`context_menu::kContextMenuSceneKind`).

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

/// @brief **Boot wiring this layer needs and does not do for itself.** Read this before wiring the
///        scene stack to `deluge.cpp`; each entry is a real setting that silently stays at its
///        default until someone makes the call.
///
/// `assigned_node_ids` above exists because `menu::assign_node_ids` had no caller outside the specs
/// and three features no-opped on an unnumbered tree. This note is the same hazard, for state that
/// cannot be fixed the same way -- a function-local static cannot invent a value that lives in
/// flash.
///
///   * **`menu::set_shortcuts_version()` (`menu/shortcuts.h`).** The pre-3.0 / current shortcut
///     layout. Legacy loads it in `FlashStorage::readSettings`, which calls
///     `soundEditor.setShortcutsVersion(buffer[60])` (`storage/flash_storage.cpp`) -- and that
///     writes `SoundEditor::shortcutsVersion`, a member of a `gui/` class this layer may not name,
///     so the port keeps its own copy beside the tables the setting selects. **Nothing calls
///     `menu::set_shortcuts_version` at boot today**, so a user who chose the pre-3.0 layout gets
///     the current one back on every power-on; `nodes::shortcuts_version` still works within a
///     session. Whoever owns boot should pass flash byte 60 through
///     (`ShortcutsVersion::V1` when it is `SHORTCUTS_VERSION_1`, i.e. 0). It is deliberately not
///     wired from `storage/` -- that would be a storage-depends-on-scenes edge, the wrong
///     direction. E2 task 11; the tests that cover the setting are
///     `sim/scenes_spec/tables_3_spec.cpp`'s batch-4 group and `shortcuts_spec.cpp`'s
///     `SHORTCUTS_VERSION_1 layer` group.
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
///         `by_kind[SceneKind::FirstApp + 2]` is `&context_menu_scene()`, and whose every other
///         slot is null.
deluge::ui::shell::SceneRegistry scene_registry();

} // namespace deluge::scenes
