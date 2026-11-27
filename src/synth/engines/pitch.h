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

#include "definitions_cxx.hpp" // kMaxSampleValue
#include "dsp/core/fixed_point.h"
#include "dsp/fixed/pitch/fine_tune.h"
#include <cstdint>
#include <optional>

namespace deluge::synth {

/// Scales @p phase_increment by @p adjustment, a pitch factor (2^24 is unison).
/// @return false when the result is too high to render (above 22.05 kHz); @p phase_increment is then not valid.
[[nodiscard]] inline bool adjust_pitch(dsp::UQ<32>& phase_increment, dsp::Q<24> adjustment) {
	if (adjustment != dsp::Q<24>::one()) {
		using dsp::Q;
		// The increment's word read as signed Q31 (base increments stay under 2^31) times the Q24 adjustment, rounded:
		// Q23, too high at 1.0 and above, else moved back to Q31 and its word read as the increment again
		const Q<23> output = mul_r(dsp::reinterpret_bits<Q<31>>(phase_increment), adjustment);
		if (output >= Q<23>::one()) {
			return false;
		}
		phase_increment = dsp::reinterpret_bits<dsp::UQ<32>>(as_wrap<Q<31>>(output));
	}
	return true;
}

/// @brief A wave's phase increment for @p transposed_note_code (the note with every transpose added), before its cents
/// and the unison detune.
/// @return None when the note is too high to render (above 22.05 kHz).
[[nodiscard]] std::optional<uint32_t> wave_phase_increment(int32_t transposed_note_code);

/// @brief A unison part's increment: @p phase_increment detuned by the part's @p detuner, or none when it reaches
/// 2^31, too high to render (above 22.05 kHz), as the base increment is held under it.
[[nodiscard]] std::optional<uint32_t> detuned_increment(uint32_t phase_increment, const dsp::fixed::FineTune& detuner);

} // namespace deluge::synth
