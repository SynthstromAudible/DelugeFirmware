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

#include "modulation/params/param.h"

class ParamManager;
class ParamManagerForTimeline;

namespace deluge::model {

/// @brief Sets the params every effects owner has (EQ, arpeggiator, stutter, mod FX, bitcrush, sidechain shape,
/// compressor threshold) to their defaults: every row of the owner's table in the shared ids.
/// @param paramManager The param manager to initialise.
/// @param defaults     Whose table's defaults: UNPATCHED_SOUND for a sound, UNPATCHED_GLOBAL for a host chain.
void initSharedParams(ParamManager* paramManager, modulation::params::Kind defaults);

/// @brief Sets a host chain's params (a kit's, the song's) to their defaults: the shared ones and every other row of
/// the host chain's table but host::chains::kNotInitialised, and marks its unpatched set global.
/// @param paramManager The param manager to initialise.
void initGlobalParams(ParamManager* paramManager);

/// @brief Sets an audio clip's params to their defaults: a host chain's, with the volume lower, as an audio track
/// starts quieter than the song and a kit's affect-entire.
/// @param paramManager The param manager to initialise.
void initAudioClipParams(ParamManagerForTimeline* paramManager);

} // namespace deluge::model
