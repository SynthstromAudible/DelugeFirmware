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

#include <array>
#include <cstdint>

/// @file
/// @brief The pitch each pinned sample's detection finds: PitchScan::result() by its bits.
namespace pitch_pin_records {

/// @brief One sample's recorded detection.
struct Record {
	const char* file; ///< Its name in the load_policy card's SAMPLES/PIT/.
	uint32_t hz_bits; ///< PitchScan::result() by its bits.
};

inline constexpr std::array<Record, 4> kRecords{{
    {"SINE55.WAV", 0x425BDEBFU},
    {"SINE440.WAV", 0x43DBE84AU},
    {"SINE1760.WAV", 0x44DBFBECU},
    {"PARTIALS.WAV", 0x4343E5B1U},
}};

} // namespace pitch_pin_records
