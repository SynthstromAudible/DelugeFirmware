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
/// @brief The bands each pinned wavetable builds, by CRC: the synthetic set in tests/integration/golden/wavetable_pins
/// and the golden presets' wavetables.
namespace wavetable_band_records {

/// @brief One wavetable's recorded build.
struct Record {
	const char* file;  ///< Its path under tests/integration/golden/ (the card holds wavetable_pins/ as SAMPLES/WTP/ and
	                   ///< presets/wavetable*/ as SAMPLES/WTG/).
	int32_t num_bands; ///< The bands it builds.
	uint32_t crc;      ///< wavetable_band_crc of the table WaveTable::setup builds from the file.
};

inline constexpr std::array<Record, 22> kRecords{{
    {"wavetable_pins/WTP_BIG.WAV", 11, 0xE4B57D43U},        {"wavetable_pins/WTP_F32.WAV", 9, 0xA1EF1FA0U},
    {"wavetable_pins/WTP_MANY.WAV", 6, 0x665B5D5FU},        {"wavetable_pins/WTP_NPOT.WAV", 8, 0x9B94039FU},
    {"wavetable_pins/WTP_ONE.WAV", 9, 0xFAA9506AU},         {"wavetable_pins/WTP_P24.WAV", 9, 0x263B7CBDU},
    {"wavetable_pins/WTP_P32.WAV", 9, 0x80AD7FE2U},         {"wavetable_pins/WTP_S32.WAV", 3, 0x05EBD28EU},
    {"wavetable_pins/WTP_S16.WAV", 2, 0x31D48F7AU},         {"wavetable_pins/WTP_S8.WAV", 1, 0x3B3D7F77U},
    {"wavetable_pins/WTP_U8.WAV", 9, 0x8E9B4F58U},          {"presets/wavetable_basic.wav", 9, 0xF6D75D3FU},
    {"presets/wavetable/morph_lfo.wav", 9, 0x9C36D926U},    {"presets/wavetable/morph_note.wav", 9, 0x9C36D926U},
    {"presets/wavetable/morph_sync.2.wav", 9, 0x04617777U}, {"presets/wavetable/morph_sync.wav", 9, 0x0A36FDC7U},
    {"presets/wavetable/scw.wav", 6, 0xD52350ACU},          {"presets/wavetable/scw_hi.wav", 6, 0xD52350ACU},
    {"presets/wavetable/static.wav", 9, 0xEF1ECB14U},       {"presets/wavetable/static_lo.wav", 9, 0xEF1ECB14U},
    {"presets/wavetable/sync.2.wav", 9, 0x04617777U},       {"presets/wavetable/sync.wav", 9, 0x0A36FDC7U},
}};

} // namespace wavetable_band_records
