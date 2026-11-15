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

/// @file
/// @brief A reverb model's whole state, as the workloads digest it: the reference (reference_reverb.h) and the kernels
/// each give one, so the records compare state without either side knowing the other's layout.

#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace deluge::dsp::test {

/// @brief Everything a Freeverb carries from one block to the next, in a canonical order: each line's words (combs
/// L1..L8, R1..R8, then allpasses L1..L4, R1..R4), each line's index in that order, the comb filter stores (L then
/// R), and the DC blocker's memory.
struct FreeverbView {
	std::array<std::span<const int32_t>, 24> lines;
	std::array<int32_t, 24> index;
	std::array<int32_t, 16> store;
	int32_t dc;
};

/// @brief Everything a plate (Mutable or Digital) carries: the ring, its write index, the LFO's two vectors as lanes,
/// and the loop and output filter memories (lp_band is 0 for Mutable, which has none).
struct PlateView {
	std::span<const float> ring;
	int32_t write;
	std::array<float, 2> lfo_y_0, lfo_y_1;
	float lp_decay_1, lp_decay_2, lp_band, hp_l, hp_r, lp_l, lp_r;
};

} // namespace deluge::dsp::test
