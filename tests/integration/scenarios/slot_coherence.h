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
#include <string>

class Song;
class Sound;

namespace deluge::harness {

/// Which equations a coherence check holds a sound to.
struct CoherenceChecks {
	bool runtime = true;   ///< the runtime's state == the record (dump_state of seededState() vs the bound state)
	bool slot = true;      ///< the slot's instrument entries == encode(record), by key
	bool snapshot = true;  ///< the snapshot == projectSnapshot() of the record
	bool controls = false; ///< the owner's controls record == the runtime's knobs
};

/// @brief The first equation @p sound breaks, as "<check> at <where>", or empty. An unbound sound breaks none.
std::string soundIncoherence(::Sound& sound, ::Song& song, CoherenceChecks checks = {});

/// @brief Every bound sound of @p song (its outputs', its hibernating instruments' and its kits' sound drums) checked;
/// each break printed as `SLOT-COHERENT FAIL leg=<leg> <owner>: <what>`.
/// @return How many broke.
int32_t expectSongCoherent(::Song& song, const char* leg, CoherenceChecks checks = {});

} // namespace deluge::harness
