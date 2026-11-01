/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

namespace deluge::synth {

/// @brief What a patch cable carries: the synth's modulation sources, numbered as the runtime and the files number
/// them. NONE also stands for a source a file names that the instrument does not know.
enum class PatchSource : uint8_t {
	LFO_GLOBAL_1,
	LFO_GLOBAL_2,
	SIDECHAIN,
	ENVELOPE_0,
	ENVELOPE_1,
	ENVELOPE_2,
	ENVELOPE_3,
	LFO_LOCAL_1,
	LFO_LOCAL_2,
	X,
	Y,
	AFTERTOUCH,
	VELOCITY,
	NOTE,
	RANDOM,
	NONE,

	/// Placeholders the runtime's lookups use, never a cable's source: a source not yet known, and no source at all.
	SOON = 254,
	NOT_AVAILABLE = 255,
};

} // namespace deluge::synth
