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

#include <array>

namespace deluge::scenes {

menu::MenuScene& menu_scene() {
	static menu::MenuScene scene;
	return scene;
}

deluge::ui::shell::SceneRegistry scene_registry() {
	// Built once, on first use, so the table is filled in before any `Shell` copies it and no
	// static-initialisation order question arises between this and `menu_scene()`'s own static.
	static const std::array<deluge::ui::shell::Scene*, deluge::ui::shell::kMaxSceneKinds> kByKind = [] {
		std::array<deluge::ui::shell::Scene*, deluge::ui::shell::kMaxSceneKinds> table{};
		table[static_cast<size_t>(deluge::ui::shell::SceneKind::FirstApp)] = &menu_scene();
		return table;
	}();
	return deluge::ui::shell::SceneRegistry{std::span<deluge::ui::shell::Scene* const>{kByKind}};
}

} // namespace deluge::scenes
