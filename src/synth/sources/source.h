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

#include "definitions_cxx.hpp"    // OscType
#include "dsp-next/core/phasor.h" // deluge::dsp::Phasor
#include "dsp-next/core/ramp.h"   // deluge::dsp::Ramp
#include "dsp-next/core/types.h"  // deluge::dsp::fixed::Buffer
#include "dsp-next/fixed/noise/cong.h"
#include "dsp-next/fixed/wavetable/wave_table_data.h" // deluge::dsp::fixed::WaveTableData (storage-independent view)
#include "synth/sources/backend.h"
#include <cstdint>
#include <optional>

namespace deluge::synth {

/// @file
/// @brief The L1 **Source** contract (the per-source render tier of Phasor → Osc → Source → Engine → Voice →
/// Instrument): one @ref Source renders one voice's worth of a single sound source (an oscillator, the wavetable,
/// FM, a sample, …) into a block, given the already-modulated control values for that block. This is the seam that
/// renders every `OscType` through composable, host-constructible
/// objects — the Plaits "engine" idea at the right altitude ("Engine" itself is reserved for the *coordinator*
/// tier above). Per-source state (phase, feedback, sample position) lives *in the concrete source* (e.g. a
/// @ref deluge::dsp::Phasor member); the app hands down only the modulated numbers, never references upward.

/// An oscillator's retrigger phase when it runs free: its phase carries on from note to note.
inline constexpr uint32_t kNoRetrigger = 0xFFFFFFFF;
/// A voice's portamento position when it is not gliding.
inline constexpr uint32_t kNoPorta = 0xFFFFFFFF;

/// @brief The already-modulated per-block control values handed to a @ref Source.
///
/// Plain values only — no engine state, and nothing reaching into model/processing/storage (so an engine can
/// be built and unit-tested with zero firmware scaffolding). Pitch/phase is engine state, set when the engine
/// is created/seeded, not passed here.
struct BlockControls {
	deluge::dsp::Ramp amplitude{0, 0}; ///< the VCA amplitude ramp (start value + per-sample increment, q31)
	/// When true, VCA-scale the wave and mix it into @c output's prior contents (the normal voice path). When
	/// false, write the raw "standalone" wave straight out (e.g. the ring-mod carrier, which is multiplied later).
	bool apply_amplitude{};
	/// Raw pulse-width modulation (UQ0.32, sign-magnitude). Zero ⇒ no pulse. Each shape derives its own pulse
	/// behaviour from this: the square turns it into a perfect (ring-modulated) pulse, while the saw/sine/
	/// triangle/analog-square turn it into pulse-width-*as-sync* (the render phase is rewritten and the wave is
	/// hard-synced to its own untouched-rate phase). That derivation lives in the shapes, not here.
	uint32_t pulse_width{};
	/// Raw (re)trigger / sync-reset phase offset. The per-wave tweaks (SINE +3·2^30, SAW +2^31) are applied
	/// inside the shapes, not here.
	uint32_t retrigger_phase{};
	/// present ⇒ **real** hard-sync this block to this master oscillator (its phase + increment, UQ0.32). The
	/// self-sync a pulse-width wave induces is derived in the shape and never appears here.
	std::optional<deluge::dsp::Phasor> sync;
	int32_t cpu_direness{}; ///< snapshot of AudioEngine::cpuDireness — drives the crude↔band-limited
	                        ///< choice (and the analog-saw downgrade) without the engine reaching into processing/
};

/// @brief The uniform outcome of rendering one @ref Source for one unison part, folding the per-engine return
/// conventions (sample's status / DX7's active-bool / the void oscillator paths) into one: keep sounding
/// (ACTIVE), unassign this source now (UNASSIGN), or skip this unison part without unassigning (SKIP — the
/// sample late-start "wait" + the osc-sync freq-too-high cases). The single uniform seam the per-source dispatch
/// folds onto.
enum class SourceRenderResult { ACTIVE, UNASSIGN, SKIP };

/// @brief A source's start at a note-on: what any source may read; each reads its own.
struct SourceNote {
	uint32_t samples_late{};   ///< How far into the note playback starts (a sample's late start)
	int32_t note_code{};       ///< The note after arpeggiation (DX7)
	uint8_t velocity{};        ///< DX7
	int32_t priority_rating{}; ///< The voice's cull rating (a sample ranks its cluster loads by it)
	void* dx_patch{};          ///< A DX7 source's patch, the opaque handle its port takes
	OscType osc_type{};        ///< The source's type as set (a live input latches its channel from it)
};

/// @brief A source's release at the note-off.
struct SourceRelease {
	int32_t priority_rating{}; ///< The voice's cull rating (a sample's loop-end release ranks its loads by it)
};

/// @brief The master oscillator a slave hard-syncs to for one block.
template <class B>
struct BasicSyncMaster {
	B::Phase phase{};     ///< The master's phase at block start
	B::Phase increment{}; ///< The master's phase increment; 0 when it is too high to render
};
using SyncMaster = BasicSyncMaster<Fixed>;

/// @brief A wavetable source's block inputs: its band data and morph index.
struct WavetableBlock {
	deluge::dsp::fixed::WaveTableData data; ///< The loaded table's bands (storage-independent view); empty for none
	int32_t index_last{};                   ///< The morph index at block start
	int32_t index_increment{};              ///< The morph index's per-sample slope
};

/// @brief A DX7 source's block inputs.
struct Dx7Block {
	int32_t amp_mod_param{}; ///< The amplitude-modulation control (the source's raw pulse-width param)
	bool patch_changed{};    ///< Re-derive the patch state this block
	int32_t note_code{};     ///< The note after arpeggiation
};

/// @brief A sample source's block inputs.
struct SampleBlock {
	uint32_t porta_envelope_pos{kNoPorta};   ///< The voice's portamento position; kNoPorta when not gliding
	int32_t priority_rating{};               ///< The voice's cull rating (the cache and the reader rank by it)
	uint32_t phase_increment_stored_value{}; ///< The phase increment before modulation (the late-start maths)
	bool pitch_mod_disallows_cache{};        ///< A source that can change during the note modulates its pitch
};

/// @brief A live-input source's block inputs.
struct InputBlock {
	int32_t interpolation_buffer_size{}; ///< The pitch shift's interpolation size for this block's increment
};

/// @brief Everything a @ref BasicSource renders one unison part of one block from: already-computed values only, no
/// engine state and nothing of the app. Each source reads the common part and its own per-type block.
template <class B>
struct BasicSourceBlock {
	B::Sample* osc_buffer{};                ///< Destination the source adds into (interleaved when @ref stereo_buffer)
	int32_t num_samples{};                  ///< Samples to render
	bool stereo_buffer{};                   ///< @ref osc_buffer is interleaved stereo: render mono, then pan
	bool stereo_unison{};                   ///< This unison part is panned
	B::Control amplitude_l{};               ///< The unison part's left pan gain
	B::Control amplitude_r{};               ///< The unison part's right pan gain
	B::Control source_amplitude{};          ///< The VCA amplitude at block start
	B::Control amplitude_increment{};       ///< The VCA amplitude's per-sample increment
	B::Phase phase_increment{};             ///< The source's pitch
	std::optional<BasicSyncMaster<B>> sync; ///< Present: hard-sync to this master this block (oscillators only)
	B::OscillatorControls controls{};       ///< The oscillator controls (amplitude, pulse width, sync, cpu direness)
	B::Sample* stereo_scratch{};            ///< Mono work buffer when @ref stereo_buffer
	int32_t stereo_scratch_clear_samples{}; ///< Samples of @ref stereo_scratch to zero before rendering into it
	B::Sample* sync_scratch{};              ///< The oscillator hard-sync work buffer
	deluge::dsp::fixed::Cong* noise{};      ///< The synth's noise stream (a live input's pitch shift draws from it)
	B::Wavetable wavetable{};
	B::Dx7 dx7{};
	B::SampleInputs sample{};
	B::Input input{};
};
using SourceBlock = BasicSourceBlock<Fixed>;

/// @brief The L1 **Source** contract: one generator in one slot of one unison part, over the numeric backend @p B.
/// Stateful: phase, feedback and playback position live in the concrete source and persist across calls.
template <class B>
class BasicSource {
public:
	BasicSource() = default;
	BasicSource(const BasicSource&) = default;
	BasicSource& operator=(const BasicSource&) = default;
	BasicSource(BasicSource&&) = default;
	BasicSource& operator=(BasicSource&&) = default;
	virtual ~BasicSource() = default;

	/// Starts the source for a note. @return false when the note cannot play.
	virtual bool note_on(const SourceNote& /*note*/) { return true; }
	/// Renders one block of @p block into @c block.osc_buffer. @return the source's outcome.
	virtual SourceRenderResult render(const BasicSourceBlock<B>& block) = 0;
	/// The note-off. @return false when the source ends now (a sample with no loop end left to play out).
	virtual bool release(const SourceRelease& /*release*/) { return true; }
	/// Lets go of whatever the source holds for its note (a reader, a pooled voice, a pitch shifter).
	virtual void unassign(bool /*deleting_song*/) {}
	/// An oscillator source's phase, which osc sync reads from the master; none for other sources.
	[[nodiscard]] virtual std::optional<typename B::Phase> current_phase() const { return std::nullopt; }
	/// Advances an oscillator source's phase by @p delta (a skipped osc-sync master still runs).
	virtual void advance_phase(B::Phase /*delta*/) {}
	/// Whether the source processes the other slot's audio rather than generating its own (a resonator excited by
	/// the other slot, Rings-style). Declared for the exciter→resonator engines to come; no source is one yet, and
	/// no engine hands one audio.
	[[nodiscard]] virtual bool takes_input() const { return false; }
};
using Source = BasicSource<Fixed>;

} // namespace deluge::synth
