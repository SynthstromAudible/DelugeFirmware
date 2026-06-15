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

#include "dsp-next/core/generator.h" // Buffer, q31
#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/osc/sine_table.h" // sine_table
#include "dsp-next/fixed/sync/resetter.h"  // resetter_reciprocal
#include "util/fixedpoint.h"               // multiply_32x32_rshift32
#include <cstddef>
#include <cstdint>
#include <optional>

namespace deluge::dsp::fixed {

/// @brief The hard-sync master: a virtual oscillator whose cycle boundaries ("crossovers")
/// restart the audible oscillator. The reciprocal used to place the seam (see
/// @ref resetter_reciprocal) is derived from @ref phase_increment, not stored here.
struct Resetter {
	uint32_t phase;
	uint32_t phase_increment;
};

/// @brief Splits a render block into hard-sync windows, one per master-oscillator cycle.
///
/// Hard sync restarts the audible oscillator at every master-cycle boundary. This is the shared
/// machinery behind @ref SyncedOscillator and the wavetable sync oscillator: pull a @ref Window at
/// a time with @ref next, fill it from its phase, and the windower blends each restart seam with a
/// half-sine. It is the cleaned, exact arithmetic of the legacy `renderOscSync` (windows filled to
/// their precise length, no padded overshoot), but as an object you iterate rather than a callback.
///
/// Each window overlaps the next by one sample — the crossover sample — which is filled again
/// ("redone") from the reset phase and then seam-blended into the cycle before it. Because the
/// blend reads the *filled* sample, @ref next finalises the window it handed out last call (blend +
/// resetter bookkeeping) before planning the next, so the loop is simply:
/// @code
///   SyncWindower windower{output, resetter, phase, increment, retrigger};
///   while (std::optional<SyncWindower::Window> w = windower.next()) {
///       // fill [w->data, w->data + w->length) from w->phase
///   }
///   phase = windower.phase();
/// @endcode
class SyncWindower {
public:
	/// One sync window to fill. @ref offset is the window's start position within the block, for
	/// fills with position-indexed state (e.g. the wavetable cross-cycle morph, which advances with
	/// output position and so reuses the same value when the crossover sample is redone).
	struct Window {
		Buffer output;
		Phasor phasor; ///< the (reset) phasor to fill this window from; pre-increment per sample.
		int32_t offset;
	};

	/// @param output the block to fill.
	/// @param resetter the master oscillator's phase + increment at the block start.
	/// @param phase the audible oscillator's starting phase (UQ0.32; pre-increment per sample).
	/// @param phase_increment the audible oscillator's per-sample phase step (UQ0.32).
	/// @param retrigger_phase the phase the audible oscillator restarts from at each crossover.
	SyncWindower(Buffer output, Resetter resetter, uint32_t phase, uint32_t phase_increment, uint32_t retrigger_phase)
	    : block_start_{output.data()}, window_{output.data()}, samples_remaining_{static_cast<int32_t>(output.size())},
	      resetter_{resetter}, resetter_divide_{resetter_reciprocal(resetter.phase_increment)}, phase_{phase},
	      phase_increment_{phase_increment}, retrigger_phase_{retrigger_phase} {}

	/// The next window to fill, or `nullopt` once the block is done. Finalises the previously
	/// handed-out window first (blends its seam, advances past the crossover), so call it in a
	/// `while` loop and fill each window before the next call.
	[[gnu::hot]] std::optional<Window> next() {
		if (done_) {
			return std::nullopt;
		}
		if (started_) {
			// Finalise the window handed out last call (the caller has filled it by now).
			// Crossfade the restart seam at its *start*, if it opened on a crossover.
			if (crossed_over_) {
				int32_t average = (*window_ >> 1) + (pre_crossover_sample_ >> 1);
				int32_t half_difference = (*window_ >> 1) - (pre_crossover_sample_ >> 1);
				int32_t sine = sine_table.value(seam_fade_ >> 1);
				*window_ = average + (multiply_32x32_rshift32(half_difference, sine) << 1);
			}

			if (!crosses_) {
				// No crossover in that (final) window: advance the phase to stay valid for the next
				// block, and finish.
				phase_ += phase_increment_ * static_cast<uint32_t>(length_);
				done_ = true;
				return std::nullopt;
			}

			// Re-base onto the crossover sample (this window overlaps it by one), and plan the restart.
			window_ += samples_to_crossover_ - 1;
			pre_crossover_sample_ = *window_;
			samples_remaining_ -= static_cast<int32_t>(samples_to_crossover_) - 1;

			// Advance the resetter past the crossover. After the first crossover its phase reads one
			// sample late, so subtract `crossed_over_` (0 the first time, 1 thereafter).
			resetter_.phase += resetter_.phase_increment * (samples_to_crossover_ - (crossed_over_ ? 1u : 0u));

			// Resetter overshoot past the boundary → fractional sample position (±0.5 as ±2^14),
			// scaled to full range; from it derive the restarted audible phase.
			seam_fade_ = multiply_32x32_rshift32(static_cast<int32_t>(resetter_.phase), resetter_divide_) << 17;
			phase_ = multiply_32x32_rshift32(seam_fade_, phase_increment_) + retrigger_phase_;
			phase_ -= phase_increment_; // back up one: we redo the crossover sample as the seam

			crossed_over_ = true;
			// Bump to 2 now: the resetter phase sits one sample later than "reality", so this window's
			// inclusive-crossover count starts one higher.
			samples_to_crossover_ = 2;
		}

		// Plan the next window: samples from its start up to *and including* the next crossover sample.
		// How far (in resetter phase) until its next cycle boundary, biased half an increment so the
		// boundary lands mid-sample; divide by the increment to get it in samples.
		uint32_t phase_to_crossover = -resetter_.phase - (resetter_.phase_increment >> 1);
		samples_to_crossover_ += (phase_to_crossover - 1) / resetter_.phase_increment;

		crosses_ = (samples_remaining_ >= static_cast<int32_t>(samples_to_crossover_));
		length_ = crosses_ ? static_cast<int32_t>(samples_to_crossover_) : samples_remaining_;
		started_ = true;
		return Window{Buffer{window_, static_cast<std::size_t>(length_)}, Phasor{phase_, phase_increment_},
		              static_cast<int32_t>(window_ - block_start_)};
	}

	/// The audible phase after the whole block — what the voice stores to continue next block.
	/// Valid once @ref next has returned `nullopt`.
	[[nodiscard]] uint32_t phase() const { return phase_; }

private:
	q31* const block_start_;
	q31* window_;
	int32_t samples_remaining_;
	Resetter resetter_;
	const int32_t resetter_divide_;
	uint32_t phase_;
	uint32_t phase_increment_;
	uint32_t retrigger_phase_;

	// Per-window state carried from the call that handed a window out to the call that finalises it.
	bool crossed_over_ = false;         // whether the outstanding window opened on a restart seam
	int32_t pre_crossover_sample_ = 0;  // last sample of the previous cycle, for the seam blend
	int32_t seam_fade_ = 0;             // crossfade position at the seam, full-scale (±1 == ±2^31)
	uint32_t samples_to_crossover_ = 1; // samples up to and including the next crossover (leading 1 = it)
	int32_t length_ = 0;                // the outstanding window's length
	bool crosses_ = false;              // whether the outstanding window ends on a crossover
	bool started_ = false;              // whether a window has been handed out yet
	bool done_ = false;
};

} // namespace deluge::dsp::fixed
