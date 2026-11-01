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

#include "dsp-next/fixed/noise/cong.h"
#include "synth/engines/engine_controls.h"
#include "synth/engines/source_levels.h"
#include "synth/voice/sound_snapshot.h"
#include "synth/voice/unison_part.h"
#include "synth/voice/voice_config.h"
#include "synth/voice/voice_inputs.h"
#include <array>
#include <cstdint>
#include <span>

namespace deluge::synth {

/// @brief A note starting or changing, as every engine reads it.
template <class B>
struct BasicEngineNote {
	std::span<typename B::UnisonPart> parts; ///< The voice's unison parts, lent for the call
	const B::Setup* setup{};                 ///< Each source's setup, the modulators' audibility
	const B::Snapshot* snapshot{};           ///< The sound's synth mode, unison layout, transposes and retrigger phases
	int32_t note_code{};                     ///< The note after arpeggiation
	uint8_t velocity{};
	uint32_t samples_late{};   ///< How far into the note playback starts
	int32_t priority_rating{}; ///< The voice's cull rating (a sample ranks its loads by it)
	bool reset{};              ///< A new voice; false when a voice is re-triggered and continues
	dsp::fixed::Cong* noise{}; ///< The synth's noise stream
};
using EngineNote = BasicEngineNote<Fixed>;

/// @brief One block of an engine: the voice's buffer, the unison parts and levels it lends, scratch, and the controls
/// the voice derived from its params for the engine's family.
template <class B>
struct BasicEngineBlock {
	B::Sample* osc_buffer{}; ///< The voice's buffer, stereo pairs when the unison is spread
	int32_t num_samples{};
	std::span<typename B::UnisonPart> parts;
	B::Levels* levels{};
	const B::Config* cfg{};
	const B::VoiceInputs* voice_block{};
	std::array<typename B::Sample*, 4> spare_buffers{}; ///< The render's work buffers; [0] is @ref osc_buffer
	std::span<typename B::Sample> sync_scratch;         ///< A synced oscillator's work buffer
	dsp::fixed::Cong* noise{};                          ///< The synth's noise stream
	B::DualSourceControls dual{};                       ///< For the dual-source engines
	B::FmControls fm{};                                 ///< For FM
};
using EngineBlock = BasicEngineBlock<Fixed>;

/// @brief What an engine's block left for the voice.
struct EngineRenderResult {
	bool did_stereo_buffer{}; ///< The engine rendered stereo pairs into the voice's buffer
	bool all_parts_ended{};   ///< Every unison part's sources ended (the voice unassigns, or fast-releases)
};

/// @brief A stage the instrument runs once per sound, after its voices: a physical model's soundboard and sympathetic
/// resonance, which no single voice can hold. Declared for the engines to come; no engine has one yet, and the
/// instrument does not run one.
template <class B>
class BasicSoundStage {
public:
	BasicSoundStage() = default;
	BasicSoundStage(const BasicSoundStage&) = delete;
	BasicSoundStage& operator=(const BasicSoundStage&) = delete;
	BasicSoundStage(BasicSoundStage&&) = delete;
	BasicSoundStage& operator=(BasicSoundStage&&) = delete;
	virtual ~BasicSoundStage() = default;
	/// Processes the sound's summed voices in place.
	virtual void render(std::span<typename B::Sample> sound_buffer) = 0;
};
using SoundStage = BasicSoundStage<Fixed>;

/// @brief The L1 **Engine** contract, over the numeric backend @p B: one voice's timbre generator over all its unison
/// parts. Each engine family relates the two source slots in its own way (dual-source engines mix, multiply or sync
/// them; a whole-voice engine generates the voice itself). The voice keeps the envelopes, modulation, filters and
/// output stage.
template <class B>
class BasicEngine {
public:
	BasicEngine() = default;
	BasicEngine(const BasicEngine&) = delete;
	BasicEngine& operator=(const BasicEngine&) = delete;
	BasicEngine(BasicEngine&&) = delete;
	BasicEngine& operator=(BasicEngine&&) = delete;
	virtual ~BasicEngine() = default;

	/// Starts the note: marks each source active in every part where it can sound, works out the phase increments,
	/// and starts each active source. @return false when the note cannot play.
	virtual bool note_on(const BasicEngineNote<B>& note) = 0;
	/// Each part's phase increments for the playing note (at note-on and on a note change); a source too high to
	/// render goes inactive.
	virtual void calculate_phase_increments(const BasicEngineNote<B>& note) = 0;
	/// A new voice's random phases for unison part @p u beyond its sources' (drawn after theirs).
	virtual void randomize_part(int32_t /*u*/, dsp::fixed::Cong& /*noise*/) {}
	/// Renders one block into the voice's buffer, which the engine starts (the noise source, or cleared) and adds into.
	virtual EngineRenderResult render(const BasicEngineBlock<B>& block) = 0;
	/// The note-off's releases of the parts' sources (a sample's loop end, a DX7's key-up).
	virtual void release(std::span<typename B::UnisonPart> /*parts*/, const B::Release& /*info*/,
	                     int32_t /*priority_rating*/) {}

	/// Whether the engine shapes the voice's amplitude itself (a physical model's decay), so the voice would skip its
	/// amplitude envelope. Declared for the engines to come; every engine here returns false, and the voice does not
	/// read it yet.
	[[nodiscard]] virtual bool owns_envelope() const { return false; }
	/// The stage this engine needs run once per sound after its voices; none here. Declared with @ref BasicSoundStage.
	[[nodiscard]] virtual BasicSoundStage<B>* sound_stage() { return nullptr; }
};
using Engine = BasicEngine<Fixed>;

} // namespace deluge::synth
