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

#include "dsp-next/core/phasor.h"
#include "util/fixedpoint.h" // multiply_32x32_rshift32
#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief A phase accumulator that hard-syncs: each step it also advances a "resetter"
/// (master) phase and, when the resetter wraps a cycle, snaps the audible phase back to the
/// corresponding point in the master's new cycle.
///
/// Interface-compatible with @ref deluge::dsp::Phasor (`advance()` returns the new phase,
/// `phase()`, `seek()`), so it drops straight into any generator templated on its phase source
/// — the crude oscillators use a plain Phasor for free-running and this for hard sync, sharing
/// one generator body. Unlike @ref SyncedOscillator (which renders in windows and crossfades
/// the seam with a half-sine), this is the **crude** per-sample reset: a hard phase jump, no
/// crossfade — cheaper and aliasing, matching the crude wave path it serves. The reset
/// arithmetic is transcribed verbatim from the legacy crude-sync loops, so it is bit-exact.
class SyncedPhasor {
	Phasor phasor_;           ///< the audible (synced) oscillator's phase
	Phasor resetter_;         ///< the master phase whose cycle boundaries reset the audible one
	int32_t resetter_divide_; ///< reciprocal of the increment; >> 47 effective (<< 17 after two q31 muls)
	uint32_t retrigger_phase_;

public:
	/// @param phasor the audible oscillator's phase accumulator
	/// @param resetter the master phase accumulator whose cycle boundaries reset @p phasor
	/// @param resetter_divide reciprocal of the audible increment (>> 47 effective)
	/// @param retrigger_phase phase the audible wave restarts from at a crossover
	SyncedPhasor(Phasor phasor, Phasor resetter, int32_t resetter_divide, uint32_t retrigger_phase)
	    : phasor_{phasor}, resetter_{resetter}, resetter_divide_{resetter_divide}, retrigger_phase_{retrigger_phase} {}

	[[gnu::always_inline]] uint32_t advance() {
		uint32_t phase = phasor_.advance();
		uint32_t resetter = resetter_.advance();
		// resetter wrapped past 2^32 this step -> a master cycle boundary (crossover).
		if (resetter < resetter_.increment()) {
			phase = (multiply_32x32_rshift32(multiply_32x32_rshift32(resetter, phasor_.increment()), resetter_divide_)
			         << 17)
			        + 1 + retrigger_phase_;
			phasor_.seek(phase);
		}
		return phase;
	}

	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }
	void seek(uint32_t phase) { phasor_.seek(phase); }
};

} // namespace deluge::dsp::fixed
