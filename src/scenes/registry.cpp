/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

/// @file
/// @brief The application's scene table. See `registry.h`.

#include "scenes/registry.h"

#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"

#include <array>

namespace deluge::scenes {

uint16_t assigned_node_ids() {
	// Fix round 2, M1. `assign_node_ids` had no non-spec caller: the application built its scene
	// table without it, and the three places `MenuScene` needs an id -- `save_state`,
	// `restore_state` and the shortcut-pad jump -- each answered 0 and quietly did nothing. Three
	// silent no-ops waiting for whoever wired this up. The registry is where the application's
	// menu tree becomes reachable, so it is where the tree gets numbered: one function-local
	// static, run once, before any `Shell` can reach the scene.
	//
	// Over `MenuScene::roots()` rather than `nodes::kAllRoots` deliberately -- `roots()` is what
	// `on_enter` resolves `SceneId::arg == 0` against and what `restore_state` looks a saved root
	// up in, so numbering exactly that set is what makes those two agree. (They are the same ten
	// nodes today; `roots()` is the one that stays right if they diverge.)
	static const uint16_t kAssigned = menu::assign_node_ids(menu::MenuScene::roots());
	return kAssigned;
}

menu::MenuScene& menu_scene() {
	static menu::MenuScene scene;
	(void)assigned_node_ids();
	return scene;
}

deluge::ui::shell::SceneRegistry scene_registry() {
	// Built once, on first use, so the table is filled in before any `Shell` copies it and no
	// static-initialisation order question arises between this and `menu_scene()`'s own static.
	static const std::array<deluge::ui::shell::Scene*, deluge::ui::shell::kMaxSceneKinds> kByKind = [] {
		std::array<deluge::ui::shell::Scene*, deluge::ui::shell::kMaxSceneKinds> table{};
		table[static_cast<size_t>(deluge::ui::shell::SceneKind::FirstApp)] = &menu_scene();
		// M1: numbered here as well as in `menu_scene()`, so building the table is enough on its
		// own -- `menu_scene()` already ran above, this is the belt to that braces and costs one
		// load of an initialised static.
		(void)assigned_node_ids();
		return table;
	}();
	return deluge::ui::shell::SceneRegistry{std::span<deluge::ui::shell::Scene* const>{kByKind}};
}

} // namespace deluge::scenes
