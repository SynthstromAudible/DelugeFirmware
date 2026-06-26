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
	/// Audio samples per timer tick, as a UQ32.32 fixed-point value ("big" = the 64-bit fixed form).
	uint64_t samples_per_timer_tick_big{0};
	/// Precomputed reciprocal of the above, so the clock path can multiply instead of divide.
	int32_t inverse_samples_per_timer_tick{0};
	/// Orders of magnitude (log2) the internal "inside world" clock runs faster than external input ticks.
	int32_t inside_world_tick_magnitude{0};
	/// Sync-scaling offset to inside_world_tick_magnitude, kept out of the displayed BPM.
	int32_t inside_world_tick_magnitude_offset_from_bpm{0};
	/// Clip whose loop length scales external input ticks (the sync master).
	Clip* sync_scaling_clip{nullptr};
};

} // namespace deluge::project
