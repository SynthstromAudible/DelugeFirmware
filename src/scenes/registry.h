#pragma once

/// @brief The application's scene table -- what `Shell` is constructed with.
///
/// One `SceneRegistry` for the whole application, holding **application scenes only**. The shell
/// installs its own `Popup`/`Console`/`Working`/`Boot`/`Loading` instances over whatever the
/// registry reported (`shell/shell.h:120-126,330-342`), and `SceneKind::Screensaver` names no
/// shell-owned scene at all, so an entry in any of those slots would simply be ignored. Kinds run
/// from `SceneKind::FirstApp` (== 16) upwards; E1 registers exactly one.

#include "shell/scene.h"

namespace deluge::scenes {

namespace menu {
class MenuScene;
}

/// @brief The application's one menu interpreter.
///
/// A function-local static rather than a namespace-scope object: it must outlive every `Shell`
/// that borrows it and must not participate in static-initialisation order across translation
/// units.
/// @return The singleton.
menu::MenuScene& menu_scene();

/// @brief The application's scene table, viewing storage this library owns.
///
/// @return A registry whose `by_kind[SceneKind::FirstApp]` is `&menu_scene()` and whose every
///         other slot is null.
deluge::ui::shell::SceneRegistry scene_registry();

} // namespace deluge::scenes
