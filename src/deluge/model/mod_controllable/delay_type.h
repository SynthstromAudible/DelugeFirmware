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

#pragma once

#include <cstdint>
#include <functional>

class AutoParam;
class ModControllableAudio;
class ParamManager;
class Song;

namespace deluge::model {

/// @brief One param's value and automation as they were before a change, so a caller can ask what could sound
/// before it. For a patch cable's amount, only the value is read: a cable counts unless it is the changed one and its
/// amount was 0.
struct ParamBefore {
	const AutoParam* param = nullptr; ///< the changed param; null when nothing is overridden
	int32_t value = 0;                ///< its value before the change
	bool automated = false;           ///< whether it was automated before the change
};

/// @return Whether a delay whose feedback lives in @p paramManager could sound.
/// @param sound Whether the manager is a Sound's: GLOBAL_DELAY_FEEDBACK is above its minimum (INT32_MIN), automated,
/// or has a patch cable reaching it. Otherwise (a kit's, an audio track's or the master's) UNPATCHED_DELAY_AMOUNT is
/// above its minimum or automated.
/// @param before One param's earlier value and automation, used in place of its current ones.
[[nodiscard]] bool delayCouldSound(ParamManager& paramManager, bool sound, const ParamBefore& before = {});

/// @brief Visits every (owner, param manager) pair of @p song that holds a delay's feedback: each instrument clip's
/// synth or kit and its kit's sound drums' note rows, each audio clip's track, the master with the song's param
/// manager, and every backed-up param manager.
void forEachDelayParamManager(Song& song, const std::function<void(ModControllableAudio&, ParamManager&)>& visit);

/// @brief Gives every owner of @p song whose file saved no delay type its type: OFF where no param manager of it could
/// sound; otherwise the type its file's `analog` gave, or DIGITAL where its file had no delay.
void resolvePendingDelayTypes(Song& song);

/// @brief resolvePendingDelayTypes for @p owner alone, as a preset load sets up its param managers.
void resolvePendingDelayType(Song& song, ModControllableAudio& owner);

} // namespace deluge::model
