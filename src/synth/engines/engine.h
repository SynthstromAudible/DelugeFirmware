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
struct EngineNote {
	std::span<UnisonPart> parts;     ///< The voice's unison parts, lent for the call
	const NoteSetup* setup{};        ///< Each source's setup, the modulators' audibility
	const SoundSnapshot* snapshot{}; ///< The sound's synth mode, unison layout, transposes and retrigger phases
	int32_t note_code{};             ///< The note after arpeggiation
	uint8_t velocity{};
	uint32_t samples_late{};   ///< How far into the note playback starts
	int32_t priority_rating{}; ///< The voice's cull rating (a sample ranks its loads by it)
	bool reset{};              ///< A new voice; false when a voice is re-triggered and continues
	dsp::fixed::Cong* noise{}; ///< The synth's noise stream
};

/// @brief One block of an engine: the voice's buffer, the unison parts and levels it lends, scratch, and the controls
/// the voice derived from its params for the engine's family.
struct EngineBlock {
	int32_t* osc_buffer{}; ///< The voice's buffer, stereo pairs when the unison is spread
	int32_t num_samples{};
	std::span<UnisonPart> parts;
	SourceLevels* levels{};
	const VoiceConfig* cfg{};
	const VoiceBlock* voice_block{};
	std::array<int32_t*, 4> spare_buffers{}; ///< The render's work buffers; [0] is @ref osc_buffer
	std::span<int32_t> sync_scratch;         ///< A synced oscillator's work buffer
	dsp::fixed::Cong* noise{};               ///< The synth's noise stream
	DualSourceControls dual;                 ///< For subtractive and ring-mod
	FmControls fm;                           ///< For FM
};

/// @brief What an engine's block left for the voice.
struct EngineRenderResult {
	bool did_stereo_buffer{}; ///< The engine rendered stereo pairs into the voice's buffer
	bool all_parts_ended{};   ///< Every unison part's sources ended (the voice unassigns, or fast-releases)
};

/// @brief The L1 **Engine** contract: one voice's timbre generator over all its unison parts. Each engine family
/// relates the two source slots in its own way (dual-source engines mix, multiply or sync them; a whole-voice engine
/// generates the voice itself). The voice keeps the envelopes, modulation, filters and output stage.
class Engine {
public:
	Engine() = default;
	Engine(const Engine&) = delete;
	Engine& operator=(const Engine&) = delete;
	Engine(Engine&&) = delete;
	Engine& operator=(Engine&&) = delete;
	virtual ~Engine() = default;

	/// Starts the note: marks each source active in every part where it can sound, works out the phase increments,
	/// and starts each active slot. @return false when the note cannot play.
	virtual bool note_on(const EngineNote& note);
	/// Each part's phase increments for the playing note (at note-on and on a note change); a source too high to
	/// render goes inactive.
	virtual void calculate_phase_increments(const EngineNote& note);
	/// A new voice's random phases for unison part @p u beyond its sources' (drawn after theirs).
	virtual void randomize_part(int32_t /*u*/, dsp::fixed::Cong& /*noise*/) {}
	/// Renders one block, adding into the voice's buffer.
	virtual EngineRenderResult render(const EngineBlock& block) = 0;
	/// The note-off's releases of the parts' sources (a sample's loop end, a DX7's key-up).
	virtual void release(std::span<UnisonPart> /*parts*/, const ReleaseInfo& /*info*/, int32_t /*priority_rating*/) {}

protected:
	/// Marks each source active in every part where it can sound. @return false (with @p refuse_silent) when a source
	/// cannot sound: ring-mod needs both.
	static bool mark_active(const EngineNote& note, bool refuse_silent);
	/// Starts each active slot for the note. @return false when one cannot start.
	static bool start_sources(const EngineNote& note);
};

} // namespace deluge::synth
