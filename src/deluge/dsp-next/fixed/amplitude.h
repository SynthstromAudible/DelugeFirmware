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

#include "dsp-next/core/mixer.h"
#include "dsp-next/core/types.h"
#include <argon.hpp>
#include <arm_neon.h>

namespace deluge::dsp::fixed {

/// @brief The voice VCA, expressed as a mixer: scales a raw wave block by a per-sample
/// ramped amplitude and sums it into the existing mix — `out = (raw * amp) + mix`.
///
/// Bit-exact port of the legacy `Oscillator::applyAmplitudeVectorToBuffer` (the q31
/// vector path): the same `vqdmulhq` doubling-high multiply, the same amplitude vector
/// (`amp + increment·{1,2,3,4}`, halved) and the same `increment << 1` per-vector step.
/// Caller pre-shifts `amplitude` exactly as the legacy call sites do; this changes no
/// output sample. (The crude scalar paths use a different VCA arithmetic — a separate
/// component — and the two are unified later as an audible, re-baselined step.)
class Amplitude : public Mixer<Amplitude, q31> {
	Argon<int32_t> amplitude_;
	Argon<int32_t> amplitude_increment_;

public:
	/// @param amplitude           starting amplitude (pre-shifted by the caller, as today)
	/// @param amplitude_increment per-sample amplitude ramp step
	Amplitude(int32_t amplitude, int32_t amplitude_increment)
	    : amplitude_{Argon<int32_t>{amplitude}.MultiplyAdd(amplitude_increment, int32x4_t{1, 2, 3, 4}) >> 1},
	      amplitude_increment_{amplitude_increment << 1} {}

	/// @param raw the freshly-generated wave block (read-only)
	/// @param mix the existing mix to accumulate into
	/// @return raw scaled by the (advancing) amplitude, summed with mix
	[[gnu::always_inline]] Argon<q31> render(Argon<q31> raw, Argon<q31> mix) {
		int32x4_t scaled = vqdmulhq_s32(amplitude_, raw);
		amplitude_ = amplitude_ + amplitude_increment_;
		return Argon<q31>{vaddq_s32(scaled, mix)};
	}
};

} // namespace deluge::dsp::fixed
