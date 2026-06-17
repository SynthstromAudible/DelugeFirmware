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

#include "dsp-next/core/phasor.h" // deluge::dsp::Phasor
#include "dsp-next/core/ramp.h"   // deluge::dsp::Ramp
#include "dsp-next/core/types.h"  // deluge::dsp::fixed::Buffer
#include <cstdint>
#include <optional>

namespace deluge::synth {

/// @file
/// @brief The L1 **Source** contract (the per-source render tier of Phasor → Osc → Source → Engine → Voice →
/// Instrument): one @ref Source renders one voice's worth of a single sound source (an oscillator, the wavetable,
/// FM, a sample, …) into a block, given the already-modulated control values for that block. This is the seam that
/// replaces the legacy `OscType` megaswitch (`dsp/oscillators/oscillator.cpp`) with composable, host-constructible
/// objects — the Plaits "engine" idea at the right altitude ("Engine" itself is reserved for the *coordinator*
/// tier above). Per-source state (phase, feedback, sample position) lives *in the concrete source* (e.g. a
/// @ref deluge::dsp::Phasor member); the app hands down only the modulated numbers, never references upward.

/// @brief The already-modulated per-block control values handed to a @ref Source.
///
/// Plain values only — no engine state, and nothing reaching into model/processing/storage (so an engine can
/// be built and unit-tested with zero firmware scaffolding). Pitch/phase is engine state, set when the engine
/// is created/seeded, not passed here.
struct BlockControls {
	deluge::dsp::Ramp amplitude; ///< the VCA amplitude ramp (start value + per-sample increment, q31)
	/// When true, VCA-scale the wave and mix it into @c output's prior contents (the normal voice path). When
	/// false, write the raw "standalone" wave straight out (e.g. the ring-mod carrier, which is multiplied later).
	bool apply_amplitude;
	/// Raw pulse-width modulation (UQ0.32, sign-magnitude). Zero ⇒ no pulse. Each shape derives its own pulse
	/// behaviour from this: the square turns it into a perfect (ring-modulated) pulse, while the saw/sine/
	/// triangle/analog-square turn it into pulse-width-*as-sync* (the render phase is rewritten and the wave is
	/// hard-synced to its own untouched-rate phase). That derivation lives in the shapes, not here.
	uint32_t pulse_width;
	/// Raw (re)trigger / sync-reset phase offset. The per-wave tweaks (SINE +3·2^30, SAW +2^31) are applied
	/// inside the shapes, not here.
	uint32_t retrigger_phase;
	/// present ⇒ **real** hard-sync this block to this master oscillator (its phase + increment, UQ0.32). The
	/// self-sync a pulse-width wave induces is derived in the shape and never appears here.
	std::optional<deluge::dsp::Phasor> sync;
	int32_t cpu_direness; ///< snapshot of AudioEngine::cpuDireness — drives the crude↔band-limited
	                      ///< choice (and the analog-saw downgrade) without the engine reaching into processing/
};

/// @brief Renders one block of one sound source. Stateful: per-source state lives in the concrete source and
/// persists across @ref renderBlock calls.
class Source {
public:
	virtual ~Source() = default;

	/// @brief Render @c output.size() samples.
	/// @param controls the already-modulated values for this block.
	/// @param output destination; when @ref BlockControls::apply_amplitude the wave is VCA-mixed *into* its
	///        prior contents, otherwise overwritten with the raw standalone wave.
	/// @param scratch caller-provided work buffer, at least as long as @p output (the engine owns no scratch /
	///        no allocator — buffers are injected, per the dsp-next convention).
	virtual void renderBlock(const BlockControls& controls, deluge::dsp::fixed::Buffer output,
	                         deluge::dsp::fixed::Buffer scratch) = 0;
};

} // namespace deluge::synth
