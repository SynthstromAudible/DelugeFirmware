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
#include "dsp-next/fixed/sync_windower.h" // Resetter, SyncWindower
#include <cstddef>
#include <cstdint>
#include <optional>

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
/// render(), because sync windows are irregular and inherently block-level. It composes a shared
/// @ref SyncWindower (which owns the window split + seam crossfade) and, for each window it yields,
/// just restarts the wrapped oscillator at the window's phase (it carries no position-indexed
/// state, so the window offset is ignored).
///
/// This is the cleaned, exact replacement for the legacy `renderOscSync` plus its
/// 4-sample-overshoot `storageFunction` callback (the windowing/crossfade arithmetic, now in
/// @ref SyncWindower, is transcribed verbatim → bit-identical; the only change is each window is
/// filled to its precise length instead of overshooting into a padded scratch buffer).
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
		// position-indexed state, so the window offset is ignored.
		SyncWindower windower{output, resetter_, phase_, phase_increment_, retrigger_phase_};
		while (std::optional<SyncWindower::Window> window = windower.next()) {
			oscillator_.seek(window->phase);
			oscillator_.renderBlock(Buffer{window->data, static_cast<std::size_t>(window->length)});
		}
		phase_ = windower.phase();
	}
};

} // namespace deluge::dsp::fixed
