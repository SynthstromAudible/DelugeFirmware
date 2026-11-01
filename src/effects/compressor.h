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

#include "dsp-next/fixed/dynamics/rms_feedback_compressor.h"
#include "effects/param_fragment.h"
#include "effects/state.h"

namespace deluge::effects {

/// @brief @p state's knobs as the dsp-next compressor takes them.
/// @param state The compressor state to read.
/// @return The knobs for the dsp-next compressor.
[[nodiscard]] constexpr dsp::fixed::CompressorKnobs compressor_knobs(const CompressorState& state) {
	return {
	    .attack = state.attack,
	    .release = state.release,
	    .threshold = state.threshold,
	    .ratio = state.ratio,
	    .sidechain_hpf = state.sidechain_hpf,
	    .blend = state.blend,
	};
}

/// @brief An owner's compressor: its CompressorState's knobs on the dsp-next compressor.
///
/// A sound's and a clip chain's threshold is a param, which the owner sets on dsp() after configure every block; the
/// song master's is its knob, which configure takes.
class Compressor {
public:
	/// @brief The threshold param: its word, used as stored, is non-negative.
	static constexpr EffectParam kThreshold{.name = "compressorThreshold", .mapping = plugin::MappingKind::UNIPOLAR};

	/// @brief Takes @p state's knobs (cheap when unchanged: the dsp-next compressor recomputes only on a change).
	/// @param state The compressor state to take the knobs from.
	void configure(const CompressorState& state);

	/// @brief The dsp-next compressor, for the owner's threshold, base gain, reset, render and LED reads.
	[[nodiscard]] dsp::fixed::RmsFeedbackCompressor& dsp() { return dsp_; }
	/// @brief The dsp-next compressor, for the owner's threshold, time-constant and LED reads.
	[[nodiscard]] const dsp::fixed::RmsFeedbackCompressor& dsp() const { return dsp_; }

private:
	dsp::fixed::RmsFeedbackCompressor dsp_;
};

} // namespace deluge::effects
