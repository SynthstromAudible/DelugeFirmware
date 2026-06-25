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

#include <cstdint>

class Clip;

namespace deluge::project {

/// Tempo / internal-vs-external tick timing of a song. A component of deluge::project::Song.
/// (Grouped struct for now; the controller still drives the clock — see Phase 3.)
struct Transport {
	uint64_t timePerTimerTickBig{0};     ///< time per internal timer tick, UQ32.32
	int32_t divideByTimePerTimerTick{0}; ///< reciprocal of the above (precomputed)
	int32_t insideWorldTickMagnitude{0}; ///< orders of magnitude the internal clock runs faster than input ticks
	int32_t insideWorldTickMagnitudeOffsetFromBPM{0}; ///< sync-scaling offset kept out of the BPM display
	Clip* syncScalingClip{nullptr}; ///< clip whose loop length scales external input ticks (the sync master)
};

} // namespace deluge::project
