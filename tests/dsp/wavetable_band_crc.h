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

#include "dsp-next/fixed/wavetable/wave_table_data.h"

#include <cstddef>
#include <cstdint>

/// @file
/// @brief The CRC of a built wavetable as the renderer sees it.

/// @brief CRC-32 (reflected, 0xEDB88320) of a built wavetable as the renderer sees it: its cycle count, then each
/// band's maxPhaseIncrement, fromCycleNumber, toCycleNumber, cycleSizeNoDuplicates and cycleSizeMagnitude, then the
/// samples of each cycle in its range (not the kWavetableDuplicateSamplesAtCycleEnd duplicates that follow each cycle).
[[nodiscard]] inline uint32_t wavetable_band_crc(const deluge::dsp::fixed::WaveTableData& view) {
	uint32_t crc = 0xFFFFFFFFU;
	auto feed = [&](const void* p, std::size_t n) {
		const auto* b = static_cast<const uint8_t*>(p);
		for (std::size_t i = 0; i < n; i++) {
			crc ^= b[i];
			for (int k = 0; k < 8; k++) {
				crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
			}
		}
	};
	const int32_t num_cycles = view.num_cycles();
	feed(&num_cycles, sizeof(num_cycles));
	for (int32_t b = 0; b < view.num_bands(); b++) {
		const deluge::dsp::fixed::WaveTableBand& band = view.band(b);
		feed(&band.maxPhaseIncrement, sizeof(band.maxPhaseIncrement));
		feed(&band.fromCycleNumber, sizeof(band.fromCycleNumber));
		feed(&band.toCycleNumber, sizeof(band.toCycleNumber));
		feed(&band.cycleSizeNoDuplicates, sizeof(band.cycleSizeNoDuplicates));
		feed(&band.cycleSizeMagnitude, sizeof(band.cycleSizeMagnitude));
		const int32_t stride = band.cycleSizeNoDuplicates + deluge::dsp::fixed::kWavetableDuplicateSamplesAtCycleEnd;
		for (int32_t c = band.fromCycleNumber; c < band.toCycleNumber; c++) {
			feed(&band.dataAccessAddress[stride * c], band.cycleSizeNoDuplicates * sizeof(int16_t));
		}
	}
	return ~crc;
}
