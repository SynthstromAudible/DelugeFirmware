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
#include "effects/delay.h"
#include "effects/filter.h"
#include "model/fx/chain_view.h"
#include "model/fx/effects_binding.h"

namespace deluge::model {

/// @brief A new host chain's settings: the low-pass filter's mode and the mod FX type set to their defaults one at a
/// time, each projected, the filters reset between them.
/// @param binding The chain's settings and render copy.
/// @param view    Which values the chain's knobs edit.
/// @param core    The chain's core.
/// @param filter  The chain's filters.
void initChannelSettings(EffectsBinding& binding, const ChainView& view, effects::ChainCore& core,
                         effects::Filter& filter);

/// @brief Gives @p delay's tapes back to the arena and leaves its margin, as a change to OFF does. An owner calls it
/// while its chain still exists.
/// @param delay The chain's delay.
void releaseDelayMemory(effects::Delay& delay);

} // namespace deluge::model
