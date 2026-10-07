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

#include "model/voice/filter_scratch.h"

#include "definitions_cxx.hpp" // SSI_TX_BUFFER_NUM_SAMPLES

#include <array>
#include <cstdint>

namespace deluge::voice {

namespace {

/// Two samples per frame, for a stereo block.
constexpr std::size_t kScratchSamples = SSI_TX_BUFFER_NUM_SAMPLES * 2;

std::array<dsp::q31, kScratchSamples> fade_scratch{};
std::array<dsp::q31, kScratchSamples> parallel_scratch{};

} // namespace

dsp::fixed::FilterScratch filter_scratch() {
	return {.fade = fade_scratch, .parallel = parallel_scratch};
}

} // namespace deluge::voice
