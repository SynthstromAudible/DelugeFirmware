#pragma once

/// @brief The application's scene table -- what `Shell` is constructed with.
///
/// One `SceneRegistry` for the whole application, holding **application scenes only**. The shell
/// installs its own `Popup`/`Console`/`Working`/`Boot`/`Loading` instances over whatever the
/// registry reported (`shell/shell.h:120-126,330-342`), and `SceneKind::Screensaver` names no
/// shell-owned scene at all, so an entry in any of those slots would simply be ignored. Kinds run
/// from `SceneKind::FirstApp` (== 16) upwards: `FirstApp` is the menu interpreter, `FirstApp + 1`
/// the rename editor (`rename::kRenameSceneKind`).

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
///         `by_kind[SceneKind::FirstApp + 1]` is `&rename_scene()`, and whose every other slot is
///         null.
deluge::ui::shell::SceneRegistry scene_registry();

} // namespace deluge::scenes
