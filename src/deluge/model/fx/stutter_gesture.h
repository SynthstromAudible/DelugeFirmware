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

#include "effects/chain_core.h"
#include "model/fx/effects_owner.h"

class ParamManagerForTimeline;

namespace deluge::model {

/// @brief Begins @p owner's stutter as its gold knob's press does, in the current song: only in the UI modes a stutter
/// begins in; a quantized stutter snaps its rate param's position to the nearest note length (4ths to 64ths) and keeps
/// the param where it is. The owner's own stutter settings play, or the song master's when it uses the song's.
/// @param owner        The owner whose chain stutters.
/// @param paramManager Its params, which hold the stutter-rate param.
void beginStutter(EffectsOwner owner, ParamManagerForTimeline& paramManager);

/// @brief Ends the current song's stutter, putting the rate param back where the gesture expects it when
/// @p paramManager is given (a quantized stutter's value before its snap; a free one's below-middle value to middle),
/// and leaves UI_MODE_STUTTERING.
/// @param paramManager The params of the owner that stuttered, or null to restore and redraw nothing.
void endStutter(ParamManagerForTimeline* paramManager);

/// @brief Ends the current song's stutter if @p core plays it, so no later chain at its address plays the tape. An
/// owner calls it at the end of its destructor's body, while its chain still exists.
/// @param core The owner's chain core.
void endStutterIfStuttering(const effects::ChainCore& core);

} // namespace deluge::model
