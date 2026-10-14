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

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/phasor.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/osc/band_limited_table_oscillator.h"
#include "dsp-next/fixed/osc/pulse_oscillator.h"
#include "dsp-next/fixed/osc/table_oscillator.h"
#include "dsp-next/fixed/sync/sync_windower.h" // Resetter, SyncWindower
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace deluge::dsp::fixed {

/// @brief Hard sync, as a component: a generator that wraps a generator.
///
/// It emits the wrapped `Osc`'s wave but forces it to restart at every @ref Resetter
/// crossover, crossfading each restart seam with a half-sine — so the audible oscillator is
/// slaved to the master's frequency (the classic "sync" timbre). `Osc` is any block generator
/// that owns a seekable @ref deluge::dsp::Phasor — it must expose `seek(uint32_t)` and
/// `renderBlock(Buffer)` — e.g. @ref TableOscillator.
///
/// The node owns the wrapped oscillator (by value, called directly), the resetter, and the audible
/// phase; it is itself a plain @ref block_generator with no vptr, reaching runtime selection through
/// @ref AsBlock like any other generator. renderBlock() is written out rather than synthesised from a
/// per-lane render(), because sync windows are irregular and inherently block-level. It composes a shared
/// @ref SyncWindower (which owns the window split + seam crossfade) and, for each window it yields,
/// just restarts the wrapped oscillator at the window's phase (it carries no position-indexed
/// state, so the window offset is ignored).
///
/// This is the cleaned, exact replacement for the legacy `renderOscSync` plus its
/// 4-sample-overshoot `storageFunction` callback (the windowing/crossfade arithmetic, now in
/// @ref SyncWindower, is transcribed verbatim → bit-identical; the only change is each window is
/// filled to its precise length instead of overshooting into a padded scratch buffer).
template <typename Osc>
class [[gnu::hot]] SyncedOscillator {
	Osc oscillator_;
	Resetter resetter_;
	uint32_t phase_; ///< audible oscillator phase; reset to the seam fade position at each crossover
	uint32_t phase_increment_;
	uint32_t retrigger_phase_;

public:
	using value_type = q31; ///< q31 wave samples.

	SyncedOscillator(Osc oscillator, uint32_t phase, uint32_t phase_increment, Resetter resetter,
	                 uint32_t retrigger_phase)
	    : oscillator_{oscillator}, resetter_{resetter}, phase_{phase}, phase_increment_{phase_increment},
	      retrigger_phase_{retrigger_phase} {}

	/// The audible phase after rendering — what the voice stores back to continue next block.
	[[nodiscard]] uint32_t phase() const { return phase_; }

	/// Set the per-block pitch: the audible increment and the master (resetter) increment, forwarding the audible
	/// increment to the wrapped oscillator (which re-selects its band if it is band-limited). Keeps both phases.
	void setIncrement(uint32_t phase_increment, uint32_t resetter_increment) {
		phase_increment_ = phase_increment;
		resetter_.phase_increment = resetter_increment;
		oscillator_.setIncrement(phase_increment);
	}

	/// @brief Fill @p output with the synced wave, one wrapped-oscillator block per sync window.
	void renderBlock(Buffer output) {
		// Each sync window is just the wrapped oscillator restarted at the window's phase; the
		// shared windower owns the split + seam crossfade. The wrapped oscillator carries no
		// position-indexed state, so the window offset is ignored.
		SyncWindower windower{output, resetter_, phase_, phase_increment_, retrigger_phase_};
		while (std::optional<SyncWindower::Window> window = windower.next()) {
			oscillator_.seek(window->phasor.phase());
			oscillator_.renderBlock(window->output);
		}
		phase_ = windower.phase();
	}
};
static_assert(block_generator<SyncedOscillator<TableOscillator>>, "a synced table oscillator fills a block");
static_assert(!std::is_polymorphic_v<SyncedOscillator<TableOscillator>>,
              "a synced table oscillator fills a block without a vptr");
static_assert(block_generator<SyncedOscillator<BandLimitedTableOscillator>>,
              "a synced band-limited oscillator fills a block");
static_assert(!std::is_polymorphic_v<SyncedOscillator<BandLimitedTableOscillator>>,
              "a synced band-limited oscillator fills a block without a vptr");
static_assert(block_generator<SyncedOscillator<PulseOscillator>>, "a synced pulse oscillator fills a block");
static_assert(!std::is_polymorphic_v<SyncedOscillator<PulseOscillator>>,
              "a synced pulse oscillator fills a block without a vptr");

} // namespace deluge::dsp::fixed
