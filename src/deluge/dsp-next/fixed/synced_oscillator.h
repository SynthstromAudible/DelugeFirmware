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

#include "dsp-next/core/generator.h"
#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/resetter.h" // Resetter reciprocal
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

/// @brief Hard sync, as a component: a generator that wraps a generator.
///
/// It emits the wrapped `Osc`'s wave but forces it to restart at every @ref Resetter
/// crossover, crossfading each restart seam with a half-sine — so the audible oscillator is
/// slaved to the master's frequency (the classic "sync" timbre). `Osc` is any block generator
/// that owns a seekable @ref deluge::dsp::Phasor — it must expose `seek(uint32_t)` and
/// `renderBlock(Buffer)` — e.g. @ref TableOscillator.
///
/// The node owns the wrapped oscillator, the resetter, and the audible phase; it is itself a
/// @ref BlockGenerator, so it composes into the oscillator registry / signal chains like any
/// other generator. renderBlock() is overridden rather than synthesised from a per-lane
/// render(), because sync windows are irregular and inherently block-level: the block is
/// split into one window per master cycle, each window filled to its *exact* length by the
/// wrapped oscillator (seeded at the reset phase) and then seam-blended into the previous one.
///
/// This is the cleaned, exact replacement for the legacy `renderOscSync` plus its
/// 4-sample-overshoot `storageFunction` callback. The windowing/crossfade arithmetic is
/// transcribed verbatim, so the output is bit-identical — the only change is that each window
/// is filled to its precise length instead of overshooting into a padded scratch buffer.
template <typename Osc>
class [[gnu::hot]] SyncedOscillator : public BlockGenerator<q31> {
	Osc oscillator_;
	Resetter resetter_;
	int32_t resetter_divide_; ///< reciprocal of the resetter increment, for placing the seam (>> 47 effective)
	uint32_t phase_;          ///< audible oscillator phase; reset to the seam fade position at each crossover
	uint32_t phase_increment_;
	uint32_t retrigger_phase_;

public:
	SyncedOscillator(Osc oscillator, uint32_t phase, uint32_t phase_increment, Resetter resetter,
	                 uint32_t retrigger_phase)
	    : oscillator_{oscillator}, resetter_{resetter}, resetter_divide_{resetter_reciprocal(resetter.phase_increment)},
	      phase_{phase}, phase_increment_{phase_increment}, retrigger_phase_{retrigger_phase} {}

	/// The audible phase after rendering — what the voice stores back to continue next block.
	[[nodiscard]] uint32_t phase() const { return phase_; }

	void renderBlock(Buffer output) override {
		q31* window = output.data();
		int32_t samples_remaining = static_cast<int32_t>(output.size());

		// Whether a crossover has already happened in this block — i.e. whether `window` opens on
		// a restart seam that must be crossfaded into the cycle that preceded it.
		bool crossed_over = false;
		int32_t pre_crossover_sample = 0; // last sample of the previous cycle, kept for the seam blend
		int32_t seam_fade = 0;            // crossfade position at the seam, full-scale (±1 == ±2^31)

		// Samples from the window start up to *and including* the next crossover sample; the
		// leading 1 accounts for that inclusive crossover sample.
		uint32_t samples_to_crossover = 1;
		while (true) {
			// How far (in resetter phase) until its next cycle boundary, biased half an increment
			// so the boundary lands mid-sample; divide by the increment to get it in samples.
			uint32_t phase_to_crossover = -resetter_.phase - (resetter_.phase_increment >> 1);
			samples_to_crossover += (phase_to_crossover - 1) / resetter_.phase_increment;

			bool crosses_within_window = (samples_remaining >= static_cast<int32_t>(samples_to_crossover));
			int32_t window_length =
			    crosses_within_window ? static_cast<int32_t>(samples_to_crossover) : samples_remaining;

			// Fill this window exactly (clean tail) from the current phase; the wrapped oscillator
			// owns the per-sample wave kernel.
			oscillator_.seek(phase_);
			oscillator_.renderBlock(Buffer{window, static_cast<std::size_t>(window_length)});

			// Crossfade the restart seam at the *start* of the window we just filled, if this
			// window opened on a crossover.
			if (crossed_over) {
				int32_t average = (*window >> 1) + (pre_crossover_sample >> 1);
				int32_t half_difference = (*window >> 1) - (pre_crossover_sample >> 1);
				int32_t sine = getSine(seam_fade >> 1);
				*window = average + (multiply_32x32_rshift32(half_difference, sine) << 1);
			}

			if (!crosses_within_window) {
				// No crossover in this (final) window: don't reset, just advance the phase to stay
				// valid for the next block, and finish.
				phase_ += phase_increment_ * window_length;
				return;
			}

			// We hit a crossover at the end of the window. Re-base onto the crossover sample (the
			// next window overlaps it by one), and plan the restart.
			window += samples_to_crossover - 1;
			pre_crossover_sample = *window;
			samples_remaining -= samples_to_crossover - 1;

			// Advance the resetter past the crossover. After the first crossover its phase reads
			// one sample late, so subtract `crossed_over` (0 the first time, 1 thereafter).
			resetter_.phase += resetter_.phase_increment * (samples_to_crossover - crossed_over);

			// Resetter overshoot past the boundary → fractional sample position (±0.5 as ±2^14),
			// scaled to full range; from it derive the restarted audible phase.
			seam_fade =
			    multiply_32x32_rshift32(static_cast<int32_t>(resetter_.phase), resetter_divide_)
			    << 17;
			phase_ = multiply_32x32_rshift32(seam_fade, phase_increment_) + retrigger_phase_;
			phase_ -= phase_increment_; // back up one: we redo the crossover sample as the seam

			crossed_over = true;
			// Bump to 2 now: the resetter phase sits one sample later than "reality", so the next
			// window's inclusive-crossover count starts one higher.
			samples_to_crossover = 2;
		}
	}
};

} // namespace deluge::dsp::fixed
