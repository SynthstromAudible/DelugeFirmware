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

#include "scenes/audio_clip/audio_clip_scene.h"
#include "scenes/browser/browser_scene.h"
#include "scenes/context_menu/context_menu_scene.h"
#include "scenes/marker/marker_scene.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/overlay/count_in_scene.h"
#include "scenes/overlay/param_popup_scene.h"
#include "scenes/overlay/screensaver_scene.h"
#include "scenes/recorder/recorder_scene.h"
#include "scenes/rename/rename_scene.h"
#include "scenes/slicing/slicer_scene.h"

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
		// `FirstApp + 1`, spelled by the scene itself (`rename::kRenameSceneKind`) rather than
		// recomputed here, so the slot and `RenameScene::kind()` cannot drift apart.
		table[static_cast<size_t>(rename::kRenameSceneKind)] = &rename::rename_scene();
		// `FirstApp + 2`, spelled by the scene itself for the same reason.
		table[static_cast<size_t>(context_menu::kContextMenuSceneKind)] = &context_menu::context_menu_scene();
		// `FirstApp + 3`, likewise.
		table[static_cast<size_t>(browser::kBrowserSceneKind)] = &browser::browser_scene();
		// `FirstApp + 4`, likewise.
		table[static_cast<size_t>(marker::kMarkerSceneKind)] = &marker::marker_scene();
		// `FirstApp + 5`, likewise.
		table[static_cast<size_t>(recorder::kRecorderSceneKind)] = &recorder::recorder_scene();
		// `FirstApp + 6`, likewise.
		table[static_cast<size_t>(overlay::kParamPopupSceneKind)] = &overlay::param_popup_scene();
		// `FirstApp + 7`, likewise.
		table[static_cast<size_t>(overlay::kCountInSceneKind)] = &overlay::count_in_scene();
		// `FirstApp + 8`, likewise.
		table[static_cast<size_t>(slicing::kSlicerSceneKind)] = &slicing::slicer_scene();
		// `FirstApp + 9`, likewise -- and the first entry in this table that is a ROOT scene
		// (`Scene::is_root()`), which is what makes `services::GlobalControls`'
		// `View::buttonAction` group reachable at all (`services/global_controls.cpp:580`).
		table[static_cast<size_t>(audio_clip::kAudioClipSceneKind)] = &audio_clip::audio_clip_scene();
		// `SceneKind::Screensaver` -- one of the SHELL's own kinds, not `FirstApp + N`, because
		// `shell::Screensaver` pushes it by name (`shell/screensaver.cpp:60`). The shell overwrites
		// the `Popup`/`Console`/`Working`/`Loading` slots with its own instances but deliberately
		// leaves this one alone (`shell/shell.cpp:177-184`), so this entry is the one that makes
		// that service's push succeed.
		table[static_cast<size_t>(overlay::kScreensaverSceneKind)] = &overlay::screensaver_scene();
		// M1: numbered here as well as in `menu_scene()`, so building the table is enough on its
		// own -- `menu_scene()` already ran above, this is the belt to that braces and costs one
		// load of an initialised static.
		(void)assigned_node_ids();
		return table;
	}();
	return deluge::ui::shell::SceneRegistry{std::span<deluge::ui::shell::Scene* const>{kByKind}};
}

} // namespace deluge::scenes
