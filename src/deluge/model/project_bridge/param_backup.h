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

namespace deluge::bridge {

/// @brief Parked ParamManagers, keyed by (ModControllableAudio, Clip).
///
/// When a Clip or ModControllable is not currently holding its ParamManager live, the manager is stashed here so its
/// automation/state survives. An engine runtime cache, held by the engine Song (`::Song::live`).
///
/// The bulk-delete operations that cooperatively yield to the audio engine (`deleteBackedUpParamManagersForClip` and
/// the two `deleteAll…`) are on the ::Song controller.
struct ParamBackup {
	deluge::fast_map<std::pair<ModControllableAudio*, Clip*>, ParamManager> map;

	/// @brief The backed-up ParamManager for exactly (modControllable, clip), or nullptr.
	///
	/// If @p stealInto is given, the entry's collections are moved into it and the entry erased.
	/// @param modControllable The owner.
	/// @param clip            The clip.
	/// @param stealInto       Optional destination to move the entry's collections into.
	/// @return The entry (or @p stealInto when stolen into), or nullptr if none.
	ParamManager* getForExactClip(ModControllableAudio* modControllable, Clip* clip, ParamManager* stealInto = nullptr);

	/// @brief A backed-up ParamManager for @p modControllable: the entry for exactly @p clip if there is one, else the
	///        owner's first entry (its null-clip entry when present).
	///
	/// If @p stealInto is given, the entry is moved into it and erased.
	/// @param modControllable The owner.
	/// @param clip            The clip whose own entry is preferred; may be null.
	/// @param stealInto       Optional destination to move the entry into.
	/// @return The entry (or @p stealInto when stolen into), or nullptr if none.
	ParamManager* getPreferablyWithClip(ModControllableAudio* modControllable, Clip* clip,
	                                    ParamManager* stealInto = nullptr);

	/// @brief Stash @p paramManager under (modControllable, clip), stealing its param collections.
	/// @param modControllable                The owner.
	/// @param clip                           The clip.
	/// @param paramManager                   The manager whose collections are stolen.
	/// @param shouldStealExpressionParamsToo Whether to steal the expression params as well.
	void backUp(ModControllableAudio* modControllable, Clip* clip, ParamManagerForTimeline* paramManager,
	            bool shouldStealExpressionParamsToo = false);

	/// @brief Erase every entry belonging to @p modControllable.
	/// @param modControllable The owner.
	void deleteForModControllable(ModControllableAudio* modControllable);
};

} // namespace deluge::bridge
