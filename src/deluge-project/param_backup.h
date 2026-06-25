/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#pragma once

#include "modulation/params/param_manager.h" // ParamManager (stored by value)
#include "util/containers.h"                 // deluge::fast_map
#include <utility>

class Clip;
class ModControllableAudio;
class ParamManagerForTimeline;

namespace deluge::project {

/// Parked ParamManagers, keyed by (ModControllableAudio, Clip). When a Clip or ModControllable is not
/// currently holding its ParamManager live, the manager is stashed here so its automation/state
/// survives. A component of deluge::project::Song.
///
/// The bulk-delete operations that need to cooperatively yield to the audio engine
/// (`deleteBackedUpParamManagersForClip` and the two `deleteAll…`) stay on the ::Song controller —
/// that AudioEngine coupling is deliberately kept out of this library.
struct ParamBackup {
	deluge::fast_map<std::pair<ModControllableAudio*, Clip*>, ParamManager> map;

	/// The backed-up ParamManager for exactly (modControllable, clip), or nullptr. If `stealInto` is
	/// given, the entry's collections are moved into it and the entry erased.
	ParamManager* getForExactClip(ModControllableAudio* modControllable, Clip* clip, ParamManager* stealInto = nullptr);

	/// A backed-up ParamManager for `modControllable`, preferring the null-clip entry, then the exact
	/// clip; nullptr if none. If `stealInto` is given, the entry is moved into it and erased.
	ParamManager* getPreferablyWithClip(ModControllableAudio* modControllable, Clip* clip,
	                                    ParamManager* stealInto = nullptr);

	/// Stash `paramManager` under (modControllable, clip), stealing its param collections.
	void backUp(ModControllableAudio* modControllable, Clip* clip, ParamManagerForTimeline* paramManager,
	            bool shouldStealExpressionParamsToo = false);

	/// Erase every entry belonging to `modControllable`.
	void deleteForModControllable(ModControllableAudio* modControllable);
};

} // namespace deluge::project
