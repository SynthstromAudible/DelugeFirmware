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
#include "dsp-next/fixed/sync_windower.h" // Resetter, for_each_sync_window
#include <cstddef>
#include <cstdint>

namespace deluge::dsp::fixed {

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
/// render(), because sync windows are irregular and inherently block-level. It delegates the
/// window split + seam crossfade to the shared @ref for_each_sync_window and, for each window,
/// just restarts the wrapped oscillator at the window's phase (it carries no position-indexed
/// state, so the window offset is ignored).
///
/// This is the cleaned, exact replacement for the legacy `renderOscSync` plus its
/// 4-sample-overshoot `storageFunction` callback (the windowing/crossfade arithmetic, now in
/// @ref for_each_sync_window, is transcribed verbatim → bit-identical; the only change is each
/// window is filled to its precise length instead of overshooting into a padded scratch buffer).
template <typename Osc>
class [[gnu::hot]] SyncedOscillator : public BlockGenerator<q31> {
	Osc oscillator_;
	Resetter resetter_;
	uint32_t phase_; ///< audible oscillator phase; reset to the seam fade position at each crossover
	uint32_t phase_increment_;
	uint32_t retrigger_phase_;

public:
	SyncedOscillator(Osc oscillator, uint32_t phase, uint32_t phase_increment, Resetter resetter,
	                 uint32_t retrigger_phase)
	    : oscillator_{oscillator}, resetter_{resetter}, phase_{phase}, phase_increment_{phase_increment},
	      retrigger_phase_{retrigger_phase} {}

	/// The audible phase after rendering — what the voice stores back to continue next block.
	[[nodiscard]] uint32_t phase() const { return phase_; }

	void renderBlock(Buffer output) override {
		// Each sync window is just the wrapped oscillator restarted at the window's phase; the
		// shared windower owns the split + seam crossfade. The wrapped oscillator carries no
		// position-indexed state, so the output offset is ignored.
		phase_ = for_each_sync_window(output, resetter_, phase_, phase_increment_, retrigger_phase_,
		                              [this](q31* window, int32_t length, uint32_t phase, int32_t /*output_offset*/) {
			                              oscillator_.seek(phase);
			                              oscillator_.renderBlock(Buffer{window, static_cast<std::size_t>(length)});
		                              });
	}
};

} // namespace deluge::dsp::fixed
