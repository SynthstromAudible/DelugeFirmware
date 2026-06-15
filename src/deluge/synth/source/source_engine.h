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
/// @brief The L1 source-engine contract: one engine renders one voice's worth of a single sound source
/// (an oscillator, the wavetable, FM, a sample, …) into a block, given the already-modulated control values
/// for that block. This is the seam that replaces the legacy `OscType` megaswitch (`dsp/oscillators/
/// oscillator.cpp`) with composable, host-constructible objects — the Plaits "engine" idea at the right
/// altitude. Per-source state (phase, feedback, sample position) lives *in the concrete engine* (e.g. a
/// @ref deluge::dsp::Phasor member); the app hands down only the modulated numbers, never references upward.

/// @brief The already-modulated per-block control values handed to a @ref SourceEngine.
///
/// Plain values only — no engine state, and nothing reaching into model/processing/storage (so an engine can
/// be built and unit-tested with zero firmware scaffolding). Pitch/phase is engine state, set when the engine
/// is created/seeded, not passed here.
struct BlockControls {
	deluge::dsp::Ramp amplitude; ///< the VCA amplitude ramp (start value + per-sample increment, q31)
	/// When true, VCA-scale the wave and mix it into @c output's prior contents (the normal voice path). When
	/// false, write the raw "standalone" wave straight out (e.g. the ring-mod carrier, which is multiplied later).
	bool apply_amplitude;
	uint32_t pulse_width;     ///< pulse-width / phase-width control (UQ0.32), as finalised by the adapter
	bool pulse_wave;          ///< render a perfect pulse (band-limited square ring-modulated with itself)
	uint32_t retrigger_phase; ///< phase offset applied on (re)trigger / sync reset
	/// present ⇒ hard-sync this block to this master oscillator (its phase + increment, UQ0.32).
	std::optional<deluge::dsp::Phasor> sync;
	uint32_t bandlimit_increment; ///< the note pitch (UQ0.32) that picks the anti-aliasing band / crude↔BL choice
	                              ///< (== the unrewritten phase increment; differs from the render pitch for
	                              ///< pulse waves, whose phase increment is rewritten to the pulse render rate)
	int32_t cpu_direness;         ///< snapshot of AudioEngine::cpuDireness — drives the crude↔band-limited
	                              ///< choice without the engine reaching into processing/
};

/// @brief Renders one block of one sound source. Stateful: per-source state lives in the concrete engine and
/// persists across @ref renderBlock calls.
class SourceEngine {
public:
	virtual ~SourceEngine() = default;

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
