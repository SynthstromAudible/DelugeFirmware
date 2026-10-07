/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

#include "dsp-next/fixed/interpolation/table_interpolation.h"
#include "util/lookuptables/lookuptables.h" // decayTableSmall4 / decayTableSmall8 (stay app-side; dsp-next may read util/)
#include <cstdint>

namespace deluge::dsp::fixed {

uint32_t LinearTableInterpolator::inverse(int32_t table_value_q15, int32_t output_fractional_bits,
                                          const uint16_t* table, int32_t table_size_log2) const {
	int32_t table_value = table_value_q15 >> 15;
	int32_t table_size = 1 << table_size_log2;

	// The table may run either way; +1 ascending, -1 descending. All the bounds tests multiply by this so
	// the same comparisons work for both directions.
	int32_t direction = (table[0] < table[table_size]) ? 1 : -1;

	if ((table_value - table[0]) * direction <= 0) {
		return 0; // at or past the low end
	}
	if ((table_value - table[table_size]) * direction >= 0) {
		return (1 << output_fractional_bits) - 1; // at or past the high end
	}

	// Binary-search for the entry interval straddling the target.
	int32_t low = 0;
	int32_t high = table_size;
	while (low + 1 < high) {
		int32_t mid = (low + high) >> 1;
		if ((table_value - table[mid]) * direction >= 0) {
			low = mid;
		}
		else {
			high = mid;
		}
	}

	// Position within the found interval, at the requested output resolution.
	uint32_t output = low << (output_fractional_bits - table_size_log2);
	output += static_cast<uint64_t>(table_value_q15 - (static_cast<uint16_t>(table[low]) << 15))
	          * (1 << (output_fractional_bits - table_size_log2))
	          / ((static_cast<uint16_t>(table[low + 1]) - static_cast<uint16_t>(table[low])) << 15);
	return output;
}

int32_t getDecay8(uint32_t input, uint8_t input_fractional_bits) {
	return linear_table_interpolator.interpolate(input, input_fractional_bits, decayTableSmall8);
}

int32_t getDecay4(uint32_t input, uint8_t input_fractional_bits) {
	return linear_table_interpolator.interpolate(input, input_fractional_bits, decayTableSmall4);
}

} // namespace deluge::dsp::fixed
