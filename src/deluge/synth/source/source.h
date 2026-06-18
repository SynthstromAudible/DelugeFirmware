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

class WaveTable; // opaque (storage-side); a @ref deluge::synth::SourceRenderContext carries it as a pass-through
                 // pointer only, so synth/ never reaches into storage/. Removed by the later band-data port.

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

/// @brief The uniform outcome of rendering one @ref Source for one unison part, folding the per-engine return
/// conventions (sample's status / DX7's active-bool / the void oscillator paths) into one: keep sounding
/// (ACTIVE), unassign this source now (UNASSIGN), or skip this unison part without unassigning (SKIP — the
/// sample late-start "wait" + the osc-sync freq-too-high cases). The single uniform seam the per-source dispatch
/// folds onto.
enum class SourceRenderResult { ACTIVE, UNASSIGN, SKIP };

/// @brief Everything a @ref Source needs to render one unison part of one source for this block — a synth-safe
/// transport of *already-computed* values (no engine state, no model/processing/storage reach). Built once at the
/// app-side adapter (`Voice::renderSourceComponent`); `WaveTable*` is a pass-through pointer only, and
/// @ref BlockControls::cpu_direness is baked in there so the AudioEngine global never enters synth/. Each concrete
/// source reads only the subset it needs; unused fields are harmless.
struct SourceRenderContext {
	int32_t* oscBuffer;      ///< destination; the source folds/sums its contribution into this (interleaved if stereo)
	int32_t numSamples;      ///< samples to render this block
	bool stereoBuffer;       ///< @c oscBuffer is interleaved stereo → render mono into scratch then pan-fold
	bool stereoUnison;       ///< this unison part is panned (drives the per-engine stereo sum)
	int32_t amplitudeL;      ///< unison pan-fold left gain (q31)
	int32_t amplitudeR;      ///< unison pan-fold right gain (q31)
	int32_t sourceAmplitude; ///< per-block start VCA amplitude (DX7's bespoke sum; oscillators get it via @c controls)
	int32_t amplitudeIncrement;        ///< per-sample VCA increment
	uint32_t phaseIncrement;           ///< per-block pitch (UQ0.32)
	bool doOscSync;                    ///< hard-sync this block to source 0 (oscillators only)
	uint32_t oscSyncPos;               ///< the master's phase for this unison part (when @c doOscSync)
	uint32_t oscSyncPhaseIncrement;    ///< the master's increment for this unison part; 0 ⇒ freq too high ⇒ SKIP
	BlockControls controls;            ///< the already-built oscillator controls (amplitude/pulse/sync/cpu_direness)
	WaveTable* waveTable;              ///< wavetable source only (pass-through; null for basic oscillators)
	int32_t waveIndexLast;             ///< wavetable morph: previous block's index
	int32_t waveIndexIncrement;        ///< wavetable morph: per-sample slope
	int32_t* stereoScratch;            ///< mono work buffer used when @c stereoBuffer (= the voice's spare buffer)
	int32_t stereoScratchClearSamples; ///< how many samples of @c stereoScratch to zero before rendering into it
	int32_t* syncScratch;              ///< the oscillator hard-sync work buffer (passed in; synth/ can't reach dsp/)
	int32_t ampModParam;               ///< DX7 amplitude-mod control (raw param value)
	bool patchChanged;                 ///< DX7: re-derive the patch state this block
	int32_t midinote;                  ///< DX7: the (post-arp) note code
};

/// @brief The L1 **Source** contract: render one unison part of one source's full contribution for this block,
/// reporting its lifecycle outcome. Stateful — per-source state (phase, feedback, sample position) lives in the
/// concrete source and persists across calls. This is the polymorphic seam `Voice::renderSourceComponent`
/// dispatches on (the oscillator block render is an implementation detail of the oscillator sources, not part of
/// this contract).
class Source {
public:
	virtual ~Source() = default;

	/// Render this source's contribution for @p ctx into @c ctx.oscBuffer; @return the per-source outcome.
	virtual SourceRenderResult renderSource(const SourceRenderContext& ctx) = 0;
};

} // namespace deluge::synth
