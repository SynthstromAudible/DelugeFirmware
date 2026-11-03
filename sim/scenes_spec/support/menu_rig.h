#pragma once

/// @file
/// @brief Shared guards for the menu's specs: the singleton reset every example needs, and the claim comparison the
///        contract specs make.

#include "scenes/menu/menu_scene.h"
#include "scenes/registry.h"
#include "shell/shell.h"
#include "ui/core/frame_context.h"

namespace deluge::scenes::spec {

/// @brief Leave `MenuScene` as a boot does: every scene popped (each one's `on_exit` runs), then the menu's
///        process-lifetime state reset.
/// @param shell The rig's shell.
inline void reset_menu_singleton(deluge::ui::shell::Shell& shell) {
	while (shell.stack().depth() > 0) {
		shell.stack().pop(deluge::ui::shell::SceneKind::Null);
	}
	menu_scene().reset_process_state();
}

/// @brief Whether what the scene says it owns is exactly what its render claimed: every per-input flag equal.
/// @param owned   `MenuScene::owned_input()`.
/// @param claimed The frame context's claims after `render_oled`.
inline bool owns_matches_claims(const deluge::ui::InputClaims& owned, const deluge::ui::InputClaims& claimed) {
	return owned.turn == claimed.turn && owned.edit0 == claimed.edit0 && owned.edit1 == claimed.edit1
	       && owned.horizontal == claimed.horizontal && owned.vertical == claimed.vertical
	       && owned.select == claimed.select && owned.back == claimed.back;
}

} // namespace deluge::scenes::spec
