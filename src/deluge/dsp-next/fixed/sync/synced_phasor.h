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

#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/sync/resetter.h" // resetter_reciprocal
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
	Phasor phasor_;         ///< the audible (synced) oscillator's phase
	Phasor resetter_;       ///< the master phase whose cycle boundaries reset the audible one
	Q<16> resetter_divide_; ///< 1 / the resetter increment read as Q31 (@ref resetter_reciprocal)
	UQ<32> retrigger_phase_;

public:
	/// @param phasor the audible oscillator's phase accumulator
	/// @param resetter the master phase accumulator whose cycle boundaries reset @p phasor
	/// @param retrigger_phase phase the audible wave restarts from at a crossover
	SyncedPhasor(Phasor phasor, Phasor resetter, UQ<32> retrigger_phase)
	    : phasor_{phasor}, resetter_{resetter},
	      resetter_divide_{Q<16>::from_raw(resetter_reciprocal(resetter.increment().raw()))},
	      retrigger_phase_{retrigger_phase} {}

	[[gnu::always_inline]] UQ<32> advance() {
		UQ<32> phase = phasor_.advance();
		const UQ<32> resetter = resetter_.advance();
		// resetter wrapped past 2^32 this step -> a master cycle boundary (crossover).
		if (resetter < resetter_.increment()) {
			// The overshoot past the boundary over the resetter increment is how far into this sample the master
			// wrapped; times the audible increment it is the phase the audible wave has gained since. The phases and
			// increments are UQ<32>, read as Q31 because the multiplies are signed (smmul): below 2^31 that is the
			// same bits at twice the value, and the doublings cancel in overshoot × increment / resetter increment.
			const auto overshoot = reinterpret_bits<Q<31>>(resetter);
			const auto increment = reinterpret_bits<Q<31>>(phasor_.increment());
			const Q<14> gained = mul(mul(overshoot, increment), resetter_divide_);
			const UQ<32> reset = add_wrap(
			    add_wrap(reinterpret_bits<UQ<32>>(as_wrap<Q<31>>(gained)), UQ<32>::from_raw(1)), retrigger_phase_);
			phase = reset;
			phasor_.seek(phase);
		}
		return phase;
	}

	[[nodiscard]] UQ<32> phase() const { return phasor_.phase(); }
	void seek(UQ<32> phase) { phasor_.seek(phase); }

	/// Set the per-block pitch: the audible increment and the master (resetter) increment, recomputing the
	/// resetter reciprocal. Keeps both accumulated phases. (Two increments because a hard-synced oscillator has
	/// two pitches — its own and the master's.)
	void set_increment(UQ<32> phasor_increment, UQ<32> resetter_increment) {
		phasor_.set_increment(phasor_increment);
		resetter_.set_increment(resetter_increment);
		resetter_divide_ = Q<16>::from_raw(resetter_reciprocal(resetter_increment.raw()));
	}
};

} // namespace deluge::dsp::fixed
