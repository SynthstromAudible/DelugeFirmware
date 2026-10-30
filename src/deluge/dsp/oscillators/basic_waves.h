/*
 * Copyright © 2017-2023 Synthstrom Audible Limited, 2025 Mark Adams
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
#include <bit>
#include <cstdint>
#include <utility>
namespace deluge::dsp {
namespace detail {
/// The highest phase increment each table number is used for, padded so a lookup past the last one never moves on
inline constexpr std::array<uint32_t, 20> kTableNumberBounds = {
    1247086,  1764571,  2494173,  3526245,   4982560,   7040929,   9988296,   14035840,  19701684,  28256363,
    40518559, 55063683, 79536431, 113025455, 165191049, 238609294, 306783378, 429496729, 715827882, 0xFFFFFFFF,
};
inline constexpr std::array<int8_t, 20> kTableSizeMagnitudes = {13, 12, 12, 11, 11, 11, 11, 11, 11, 11,
                                                                11, 11, 11, 11, 10, 10, 10, 10, 9,  9};
/// The lowest table number for each bit width of the phase increment: how many bounds are below 2^(width - 1)
inline constexpr auto kFirstTableForBitWidth = [] {
	std::array<uint8_t, 33> first{};
	for (uint32_t width = 1; width <= 32; width++) {
		for (uint32_t bound : kTableNumberBounds) {
			first[width] += bound < (1u << (width - 1));
		}
	}
	return first;
}();
} // namespace detail

/**
 * @brief Get a table number and size, depending on the increment
 *
 * The bounds go up by about sqrt(2) per table, so there are at most two of them in each power of two: the bit width
 * gets within two tables, and two compares finish it. This used to be a chain of 20 compares, and it's called for
 * every oscillator render.
 *
 * @return table_number, table_size
 */
[[gnu::always_inline]] constexpr std::pair<int32_t, int32_t> getTableNumber(uint32_t phaseIncrement) {
	int32_t table = detail::kFirstTableForBitWidth[std::bit_width(phaseIncrement)];
	table += phaseIncrement > detail::kTableNumberBounds[table];
	table += phaseIncrement > detail::kTableNumberBounds[table];
	return {table, detail::kTableSizeMagnitudes[table]};
}
void renderWave(const int16_t* __restrict__ table, int32_t tableSizeMagnitude, int32_t amplitude,
                int32_t* __restrict__ outputBuffer, int32_t* bufferEnd, uint32_t phaseIncrement, uint32_t phase,
                bool applyAmplitude, uint32_t phaseToAdd, int32_t amplitudeIncrement);
void renderPulseWave(const int16_t* __restrict__ table, int32_t tableSizeMagnitude, int32_t amplitude,
                     int32_t* __restrict__ outputBuffer, int32_t* bufferEnd, uint32_t phaseIncrement, uint32_t phase,
                     bool applyAmplitude, uint32_t phaseToAdd, int32_t amplitudeIncrement);
uint32_t renderCrudeSawWaveWithAmplitude(int32_t* thisSample, int32_t* bufferEnd, uint32_t phaseNowNow,
                                         uint32_t phaseIncrementNow, int32_t amplitudeNow, int32_t amplitudeIncrement,
                                         int32_t numSamples);
uint32_t renderCrudeSawWaveWithoutAmplitude(int32_t* thisSample, int32_t* bufferEnd, uint32_t phaseNowNow,
                                            uint32_t phaseIncrementNow, int32_t numSamples);
extern const int16_t* sawTables[20];
extern const int16_t* squareTables[20];
extern const int16_t* analogSquareTables[20];
extern const int16_t* analogSawTables[20];

} // namespace deluge::dsp
