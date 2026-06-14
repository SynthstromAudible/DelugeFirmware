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
#include "dsp-next/fixed/resetter.h" // resetter_reciprocal
#include "util/fixedpoint.h"         // multiply_32x32_rshift32
#include "util/waves.h"              // getSine
#include <cstddef>
#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief The hard-sync master: a virtual oscillator whose cycle boundaries ("crossovers")
/// restart the audible oscillator. The reciprocal used to place the seam (see
/// @ref resetter_reciprocal) is derived from @ref phase_increment, not stored here.
struct Resetter {
	uint32_t phase;
	uint32_t phase_increment;
};

/// @brief Split a render block into hard-sync windows and drive a fill for each, blending seams.
///
/// Hard sync restarts the audible oscillator at every master-cycle boundary. This walks the block
/// one window per master cycle, asks the caller to fill each window from the (reset) phase, and
/// crossfades each restart seam with a half-sine — the shared machinery behind
/// @ref SyncedOscillator and @ref WavetableSyncOscillator. It is the cleaned, exact arithmetic of
/// the legacy `renderOscSync` (windows filled to their precise length, no padded overshoot).
///
/// Windows overlap by one sample — the crossover sample — which is filled again ("redone") from the
/// reset phase and then seam-blended into the cycle that preceded it.
///
/// @param output the block to fill.
/// @param resetter the master oscillator's phase + increment at the block start.
/// @param phase the audible oscillator's starting phase (UQ0.32; pre-increment per sample).
/// @param phase_increment the audible oscillator's per-sample phase step (UQ0.32).
/// @param retrigger_phase the phase the audible oscillator restarts from at each crossover.
/// @param fill invoked once per window as `fill(q31* window, int32_t length, uint32_t phase,
///        int32_t output_offset)`: fill `[window, window + length)` from @p phase (pre-increment
///        per sample). @c output_offset is the window's offset from the block start, for fills with
///        position-indexed state (e.g. the wavetable cross-cycle morph, which advances with output
///        position and so reuses the same value when the crossover sample is redone).
/// @returns the audible phase after the block — what the voice stores to continue next block.
template <typename Fill>
[[gnu::hot]] uint32_t for_each_sync_window(Buffer output, Resetter resetter, uint32_t phase, uint32_t phase_increment,
                                           uint32_t retrigger_phase, Fill&& fill) {
	q31* const block_start = output.data();
	q31* window = block_start;
	int32_t samples_remaining = static_cast<int32_t>(output.size());
	const int32_t resetter_divide = resetter_reciprocal(resetter.phase_increment);

	// Whether a crossover has already happened in this block — i.e. whether `window` opens on a
	// restart seam that must be crossfaded into the cycle that preceded it.
	bool crossed_over = false;
	int32_t pre_crossover_sample = 0; // last sample of the previous cycle, kept for the seam blend
	int32_t seam_fade = 0;            // crossfade position at the seam, full-scale (±1 == ±2^31)

	// Samples from the window start up to *and including* the next crossover sample; the leading 1
	// accounts for that inclusive crossover sample.
	uint32_t samples_to_crossover = 1;
	while (true) {
		// How far (in resetter phase) until its next cycle boundary, biased half an increment so the
		// boundary lands mid-sample; divide by the increment to get it in samples.
		uint32_t phase_to_crossover = -resetter.phase - (resetter.phase_increment >> 1);
		samples_to_crossover += (phase_to_crossover - 1) / resetter.phase_increment;

		bool crosses_within_window = (samples_remaining >= static_cast<int32_t>(samples_to_crossover));
		int32_t window_length =
		    crosses_within_window ? static_cast<int32_t>(samples_to_crossover) : samples_remaining;

		// Fill this window exactly (clean tail) from the current phase; the caller owns the wave.
		fill(window, window_length, phase, static_cast<int32_t>(window - block_start));

		// Crossfade the restart seam at the *start* of the window we just filled, if it opened on a
		// crossover.
		if (crossed_over) {
			int32_t average = (*window >> 1) + (pre_crossover_sample >> 1);
			int32_t half_difference = (*window >> 1) - (pre_crossover_sample >> 1);
			int32_t sine = getSine(seam_fade >> 1);
			*window = average + (multiply_32x32_rshift32(half_difference, sine) << 1);
		}

		if (!crosses_within_window) {
			// No crossover in this (final) window: don't reset, just advance the phase to stay valid
			// for the next block, and finish.
			phase += phase_increment * window_length;
			return phase;
		}

		// We hit a crossover at the end of the window. Re-base onto the crossover sample (the next
		// window overlaps it by one), and plan the restart.
		window += samples_to_crossover - 1;
		pre_crossover_sample = *window;
		samples_remaining -= samples_to_crossover - 1;

		// Advance the resetter past the crossover. After the first crossover its phase reads one
		// sample late, so subtract `crossed_over` (0 the first time, 1 thereafter).
		resetter.phase += resetter.phase_increment * (samples_to_crossover - crossed_over);

		// Resetter overshoot past the boundary → fractional sample position (±0.5 as ±2^14), scaled
		// to full range; from it derive the restarted audible phase.
		seam_fade = multiply_32x32_rshift32(static_cast<int32_t>(resetter.phase), resetter_divide) << 17;
		phase = multiply_32x32_rshift32(seam_fade, phase_increment) + retrigger_phase;
		phase -= phase_increment; // back up one: we redo the crossover sample as the seam

		crossed_over = true;
		// Bump to 2 now: the resetter phase sits one sample later than "reality", so the next
		// window's inclusive-crossover count starts one higher.
		samples_to_crossover = 2;
	}
}

} // namespace deluge::dsp::fixed
