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

#include "definitions_cxx.hpp" // PolyphonyMode
#include "plugin/types/voice_priority.h"
#include "synth/sources/source_state.h"

#include <cstdint>

namespace deluge::synth {

/// @brief How many unison parts each voice plays, how far apart they are tuned and how wide they spread.
struct UnisonState {
	uint8_t count{1};
	int8_t detune{8};
	uint8_t spread{0};
	bool operator==(const UnisonState&) const = default;
};

/// @brief How a sound's voices are allocated and tuned as a whole.
struct VoicingState {
	PolyphonyMode polyphony{PolyphonyMode::POLY};
	uint8_t max_voices{8};
	plugin::VoicePriority priority{plugin::VoicePriority::MEDIUM};
	Tuning master;
	UnisonState unison;
	bool operator==(const VoicingState&) const = default;
};

} // namespace deluge::synth
