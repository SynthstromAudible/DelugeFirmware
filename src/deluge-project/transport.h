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
#include <optional>

namespace deluge::project {

/// @brief Tempo / internal-vs-external tick timing of a song. A component of deluge::project::Song.
///
/// The controller drives the clock; this holds its persistent state.
struct Transport {
	/// Audio samples per timer tick, as a UQ32.32 fixed-point value ("big" = the 64-bit fixed form).
	uint64_t samples_per_timer_tick_big{0};
	/// Precomputed reciprocal of the above, so the clock path can multiply instead of divide.
	int32_t inverse_samples_per_timer_tick{0};
	/// Orders of magnitude (log2) the internal "inside world" clock runs faster than external input ticks.
	int32_t inside_world_tick_magnitude{0};
	/// Sync-scaling offset to inside_world_tick_magnitude, kept out of the displayed BPM.
	int32_t inside_world_tick_magnitude_offset_from_bpm{0};
	/// The clip whose length scales the input clock (its durable id), or none. The tick magnitude above follows it.
	std::optional<uint32_t> sync_scaling_clip_id;

	/// @brief Equal when the persistent fields are; the reciprocal is derived from the tick length.
	bool operator==(const Transport& other) const {
		return samples_per_timer_tick_big == other.samples_per_timer_tick_big
		       && inside_world_tick_magnitude == other.inside_world_tick_magnitude
		       && inside_world_tick_magnitude_offset_from_bpm == other.inside_world_tick_magnitude_offset_from_bpm
		       && sync_scaling_clip_id == other.sync_scaling_clip_id;
	}
};

} // namespace deluge::project
