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

inline constexpr std::array<Record, 20> kRecords{{
    {"wavetable_pins/WTP_BIG.WAV", 11, 0x423EE312U},        {"wavetable_pins/WTP_F32.WAV", 9, 0x357D2E21U},
    {"wavetable_pins/WTP_MANY.WAV", 6, 0xAFEB2835U},        {"wavetable_pins/WTP_NPOT.WAV", 8, 0xAE60E2C8U},
    {"wavetable_pins/WTP_ONE.WAV", 9, 0xEAB8C89CU},         {"wavetable_pins/WTP_P24.WAV", 9, 0x5602C748U},
    {"wavetable_pins/WTP_P32.WAV", 9, 0x001DA2FCU},         {"wavetable_pins/WTP_S32.WAV", 3, 0x23A48755U},
    {"wavetable_pins/WTP_U8.WAV", 9, 0x1A097ED9U},          {"presets/wavetable_basic.wav", 9, 0xE6C6C5C9U},
    {"presets/wavetable/morph_lfo.wav", 9, 0xF93F1062U},    {"presets/wavetable/morph_note.wav", 9, 0xF93F1062U},
    {"presets/wavetable/morph_sync.2.wav", 9, 0xC6FA5EDAU}, {"presets/wavetable/morph_sync.wav", 9, 0x7A0F4632U},
    {"presets/wavetable/scw.wav", 6, 0xD9A58908U},          {"presets/wavetable/scw_hi.wav", 6, 0xD9A58908U},
    {"presets/wavetable/static.wav", 9, 0x107DA600U},       {"presets/wavetable/static_lo.wav", 9, 0x107DA600U},
    {"presets/wavetable/sync.2.wav", 9, 0xC6FA5EDAU},       {"presets/wavetable/sync.wav", 9, 0x7A0F4632U},
}};

} // namespace wavetable_band_records
