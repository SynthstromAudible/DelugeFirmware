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

#include "synth/engines/pitch.h"
#include "util/lookuptables/lookuptables.h" // noteFrequencyTable

namespace deluge::synth {

std::optional<uint32_t> wave_phase_increment(int32_t transposed_note_code) {
	int32_t note_within_octave = static_cast<uint16_t>(transposed_note_code + 240 - 4) % 12;
	int32_t octave = (transposed_note_code + 120 - 4) / 12;

	int32_t shift_right_amount = 20 - octave;
	if (shift_right_amount >= 0) {
		return noteFrequencyTable[note_within_octave] >> shift_right_amount;
	}
	return std::nullopt;
}

std::optional<uint32_t> detuned_increment(uint32_t phase_increment, const dsp::fixed::FineTune& detuner) {
	// The tuner works on signed increments: below 2^31 the bits are the same
	const auto detuned = static_cast<uint32_t>(detuner.apply(static_cast<int32_t>(phase_increment)));
	if (detuned >= (uint32_t{1} << 31)) {
		return std::nullopt;
	}
	return detuned;
}

} // namespace deluge::synth
