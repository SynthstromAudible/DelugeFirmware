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

#include "definitions_cxx.hpp" // kNumSources
#include <array>
#include <cstdint>

namespace deluge::synth {

/// @brief The source levels' de-zipper state, which FM and the dual-source engines share. The voice owns it and lends
/// it to whichever engine plays the block, so a mode change under a live voice continues from it as before.
struct SourceLevels {
	std::array<int32_t, kNumSources> amplitudes_last_time{}; ///< Each source's amplitude at the last block's end
	std::array<uint32_t, kNumSources>
	    wave_indexes_last_time{};    ///< Each source's wavetable index at the last block's end
	int32_t filter_gain_last_time{}; ///< The filter gain the source amplitudes were last folded with
};

} // namespace deluge::synth
