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
#pragma once

#include "dsp-next/fixed/interpolation/table_interpolation.h"
#include "util/lookuptables/lookuptables.h" // sineWaveSmall (stays app-side; dsp-next may read util/)
#include <cstdint>

namespace deluge::dsp::fixed {

/// @file
/// @brief The control-rate sine evaluator: a sine value sampled from the `sineWaveSmall` LUT at an arbitrary
/// phase. Used wherever the synth needs "sine at this phase" rather than a free-running oscillator — the LFO's
/// sine waveform, the envelope's sine-shaped fast-release, the oscillator-sync seam crossfade, and GUI easing.
///
/// This is **not** the vector @ref sine (dsp-next/fixed/osc/sine.h): that one is the SIMD FM-synthesis sine over
/// a different (interleaved-diff) table. They are deliberately two evaluators with different call shapes; the
/// `SineTable` / `sine_table` naming keeps them unambiguous. Reconciling the two is a behaviour-changing job left
/// to the re-baselined audible pass.

/// @brief A sine wave read from the shared `sineWaveSmall` LUT with linear interpolation. Stateless, mirroring
/// @ref windowed_sinc_kernel / @ref linear_table_interpolator: one shared instance, owns no phase.
class SineTable {
public:
	/// @brief Sine of @p phase.
	/// @param phase Phase as fixed-point with @p phase_fractional_bits fractional bits (the default 32 is the
	///        usual UQ0.32 where 2^32 == one cycle).
	/// @param phase_fractional_bits Bit-width of @p phase.
	/// @return The sine sample as q31 (the LUT is full-scale signed, blended at Q16 then summed to q31).
	[[nodiscard, gnu::always_inline]] int32_t value(uint32_t phase, uint8_t phase_fractional_bits = 32) const {
		return linear_table_interpolator.interpolateSigned(phase, phase_fractional_bits, sineWaveSmall, 8);
	}
};

/// The shared stateless instance (matches @ref linear_table_interpolator).
inline constexpr SineTable sine_table{};

} // namespace deluge::dsp::fixed
