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

#include "effects/sound_chain.h"

#include <cstdint>

class ParamManager;
class Sound;

/// @brief A sound's post-voice chain inputs for one block: its matrix's finals and its unpatched params as
/// Sound::render resolves them, its render copy of its settings, and the song's stutter while the sound is the one
/// stuttering.
/// @param sound The sound whose chain inputs are gathered.
/// @param paramManager The sound's param manager (its unpatched params are read).
/// @param delay The block's delay working state, which the sound set up on its chain's delay before its voices.
/// @param anyVoices Whether the sound has voices.
/// @param reverbSendAmount The sound's reverb send amount, Q31.
/// @return The inputs for the sound's chain for this block.
[[nodiscard]] deluge::effects::SoundChainInputs soundChainInputs(Sound& sound, ParamManager& paramManager,
                                                                 const deluge::effects::Delay::State& delay,
                                                                 bool anyVoices, int32_t reverbSendAmount);
