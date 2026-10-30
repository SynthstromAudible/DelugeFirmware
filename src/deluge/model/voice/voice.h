/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "dsp-next/fixed/filter/filter_set.h"
#include "effects/saturation.h"
#include "model/snapshot/sound_snapshot.h"
#include "model/voice/fm_engine.h"
#include "model/voice/ringmod_engine.h"
#include "model/voice/source_component_render.h" // deluge::synth::SourceRenderResult
#include "model/voice/subtractive_engine.h"
#include "model/voice/voice_config.h"
#include "model/voice/voice_inputs.h"
#include "model/voice/voice_sample_playback_guide.h"
#include "model/voice/voice_unison_part.h"
#include "modulation/params/param.h"
#include "synth/modulation/envelope.h"
#include "synth/modulation/lfo.h"
#include "synth/modulation/param_plane.h"
#include "synth/modulation/patcher.h"
#include <bitset>
#include <compare>
#include <memory>
#include <span>

namespace deluge::dsp {
struct StereoSample;
} // namespace deluge::dsp
using StereoSample = deluge::dsp::StereoSample;
using SoundSnapshot = deluge::snapshot::SoundSnapshot;
class ModelStackWithSoundFlags;
class ParamManagerForTimeline;
using namespace deluge;
class Voice final {
public:
	Voice(Sound& sound);
	~Voice() { setAsUnassigned(nullptr); }
	Patcher patcher;

	// Stores all oscillator positions and stuff, for each Source within each Unison too
	std::array<VoiceUnisonPart, kMaxNumVoicesUnison> unisonParts{};

	// Stores overall info on each Source (basically just sample memory bounds), for the play-through associated with
	// this Voice right now.
	std::array<VoiceSamplePlaybackGuide, kNumSources> guides{};

	Sound& sound; // This is a reference to the Sound that owns this Voice

	// The per-voice modulation data plane: the *local* param final values (specific to this Voice) plus the patch
	// source values. At the start of sourceValues are local copies of the "global" sources — cheaper to copy them
	// here than to make the Patcher pick and choose where to look.
	VoiceParams params;

	std::bitset<kNumExpressionDimensions> expressionSourcesCurrentlySmoothing{};
	std::bitset<kNumExpressionDimensions> expressionSourcesFinalValueChanged{};
	std::array<int32_t, kNumExpressionDimensions> localExpressionSourceValuesBeforeSmoothing{};

	std::array<Envelope, kNumEnvelopes> envelopes{};

	// Voice-cull timestamp for the amplitude envelope (envelopes[0]): the voice clock's tick when it entered its
	// stage. A voice-scheduling concern, not envelope DSP — the Envelope reports stage changes (takeStageChanged())
	// and the Voice stamps them. Read by priority_rating().
	uint32_t envelope0TimeEntered{};
	LFO lfo2;
	LFO lfo4;

	dsp::fixed::FilterSet filterSet;

	// The ring-mod synth engine: owns the cross-source ring-mod relationship (renders both sources + combines).
	// Stateless for now — the VA/wavetable components stay owned per-source on the VoiceUnisonParts.
	RingModEngine ringModEngine;

	// The subtractive synth engine: owns the per-source render loop + osc-sync + stereo dance. Transitional —
	// it delegates the per-source render back to the public renderBasicSource.
	SubtractiveEngine subtractiveEngine;

	// The FM synth engine: owns one unison part's FM topology (modulators + carriers + feedback). Transitional —
	// it reads the voice's params and unison parts through Voice& and calls the static Voice::adjustPitch.
	FMEngine fmEngine;

	// Contains what used to be called noteCodeBeforeArpeggiation, and fromMIDIChannel
	std::array<int32_t, 2> inputCharacteristics{};
	int32_t noteCodeAfterArpeggiation{};

	uint32_t portaEnvelopePos{};
	int32_t portaEnvelopeMaxAmplitude{};

	/// The output stage's saturation; its memory starts at 0 and is set silent when the envelopes reset.
	deluge::effects::Saturation saturation_{0u};

	int32_t overallOscAmplitudeLastTime{};
	std::array<int32_t, kNumSources> sourceAmplitudesLastTime{};
	// modulatorAmplitudeLastTime moved to FMEngine (FM-exclusive de-zipper state) — see model/voice/fm_engine.h.
	std::array<uint32_t, kNumSources> sourceWaveIndexesLastTime{};

	int32_t filterGainLastTime{};
	bool doneFirstRender{};
	bool previouslyIgnoredNoteOff{};

	uint32_t orderSounded{};

	int32_t overrideAmplitudeEnvelopeReleaseRate{};

	bool justCreated{false};

	uint32_t getLocalLFOPhaseIncrement(const LFOConfig& config, deluge::modulation::params::Local param,
	                                   uint32_t syncedPhaseIncrement);
	void setAsUnassigned(ModelStackWithSoundFlags* modelStack, bool deletingSong = false);
	/// Scale @p phaseIncrement by @p adjustment (a pitch-adjust patch value); @return false if it overflows (the
	/// note frequency is too high and the source/modulator should be dropped). Pure — used by Voice's per-source
	/// pitch math and by FMEngine for its modulators.
	static bool adjustPitch(uint32_t& phaseIncrement, int32_t adjustment);
	bool render(const ControlBlock& controlBlock, int32_t* soundBuffer, int32_t numSamples, bool soundRenderingInStereo,
	            bool applyingPanAtVoiceLevel, uint32_t sourcesChanged, int32_t externalPitchAdjust,
	            const VoiceConfig& cfg, const deluge::synth::VoiceBlock& block, std::array<int32_t*, 4> spareBuffers);

	// --- The voice's lifecycle as its sound drives it (voice_setup.cpp): each builds what the kernel lifecycle below
	// reads from the sound, then runs it.
	void calculatePhaseIncrements(ModelStackWithSoundFlags* modelStack);
	bool sampleZoneChanged(ModelStackWithSoundFlags* modelStack, int32_t s, MarkerType markerType);
	bool noteOn(ModelStackWithSoundFlags* modelStack, int32_t newNoteCodeBeforeArpeggiation,
	            int32_t newNoteCodeAfterArpeggiation, uint8_t velocity, uint32_t newSampleSyncLength, int32_t ticksLate,
	            uint32_t samplesLate, bool resetEnvelopes, int32_t fromMIDIChannel, const int16_t* mpeValues);
	void noteOff(ParamManagerForTimeline* paramManager, bool noteTailsAllowed, bool allowReleaseStage = true);
	void randomizeOscPhases(const Sound& sound);
	void changeNoteCode(ModelStackWithSoundFlags* modelStack, int32_t newNoteCodeBeforeArpeggiation,
	                    int32_t newNoteCodeAfterArpeggiation, int32_t newInputMIDIChannel, const int16_t* newMPEValues);
	void expressionEventImmediate(const Sound& sound, int32_t voiceLevelValue, int32_t s);

	// --- The kernel lifecycle, from what it is handed.

	/// Starts @p event's note. @p num_active_voices is the sound's voice count, which the cull order ranks by.
	/// @return false if the note cannot play and the voice must be unassigned again.
	bool note_on(const deluge::synth::NoteSetup& setup, const SoundSnapshot& snapshot,
	             const deluge::synth::NoteOnEvent& event, deluge::synth::VoiceClock clock, size_t num_active_voices);
	/// Moves the playing note to another, gliding when @p setup says to.
	void change_note(const deluge::synth::NoteSetup& setup, const SoundSnapshot& snapshot,
	                 int32_t noteBeforeArpeggiation, int32_t noteAfterArpeggiation, int32_t midiChannel,
	                 std::span<const int16_t, kNumExpressionDimensions> mpe);
	/// Each source's phase increment for the playing note, in each unison part; a source too high to render is made
	/// inactive.
	void calculate_phase_increments(const deluge::synth::NoteSetup& setup, const SoundSnapshot& snapshot);
	/// Releases the note: each envelope from its sustain where it has one, or at once when there is no release stage.
	/// With note tails not allowed, the note-off is only remembered.
	void note_off(const deluge::synth::ReleaseInfo& release, bool allowReleaseStage, deluge::synth::VoiceClock clock);
	void randomize_osc_phases(const SoundSnapshot& snapshot);
	void expressionEventImmediate(int32_t monophonicValue, int32_t voiceLevelValue, int32_t s);
	bool hasReleaseStage();
	void unassignStuff(bool deletingSong);
	/// The cull order's rating: higher is culled sooner. Ranks by @p voice_priority, then by @p num_active_voices (the
	/// sound's voice count), then by the amplitude envelope's stage, then by when it entered it.
	[[nodiscard]] uint32_t priority_rating(VoicePriority voice_priority, size_t num_active_voices) const;
	/// @ref priority_rating for this voice's sound as it is now.
	[[nodiscard]] uint32_t getPriorityRating() const;

	/// Stamp @ref envelope0TimeEntered from @p clock if the amplitude envelope (envelopes[0]) has just changed stage —
	/// call after any operation that may transition it.
	void stampEnvelope0StageEntry(deluge::synth::VoiceClock clock);
	/// Re-stamp while in sustain, to lower a drone note's cull likelihood.
	void resetEnvelope0TimeEntered(deluge::synth::VoiceClock clock);

	void expressionEventSmooth(int32_t newValue, int32_t s);

	/// Release immediately with provided release rate
	/// Returns whether voice should still be left active
	bool doFastRelease(uint32_t releaseIncrement, deluge::synth::VoiceClock clock);

	/// Sets envelope to off (will interpolate through this render window).
	/// Returns whether voice should still be left active
	bool doImmediateRelease(deluge::synth::VoiceClock clock);

	/// Already fading at (or faster than) the soft-cull rate. Cull victim selection and the voice limit
	/// must treat such a voice as already gone, or it soaks up culls meant for live voices (issue #4721).
	[[nodiscard]] bool isCullFading() const {
		return envelopes[0].stage() >= Envelope::Stage::FAST_RELEASE
		       && envelopes[0].fastReleaseRate() >= SOFT_CULL_INCREMENT;
	}

	bool speedUpRelease(deluge::synth::VoiceClock clock, uint32_t minFastReleaseIncrement = SOFT_CULL_INCREMENT);
	bool shouldBeDeleted() { return delete_this_voice_; }

	// This compares based on the priority of two voices
	[[nodiscard]] std::strong_ordering operator<=>(const Voice& other) const {
		return this->getPriorityRating() <=> other.getPriorityRating();
	}

	// The per-source render driver + the unison-inactive predicate. Public (not private) so SubtractiveEngine can
	// drive them without being a friend of Voice. Transitional — the per-source loop relocates into the engine
	// layer at the synth::Voice lift; renderSourceComponent (the dispatch it calls) stays private.
	void renderBasicSource(const ControlBlock& controlBlock, const deluge::synth::VoiceBlock& block, int32_t s,
	                       int32_t* oscBuffer, int32_t numSamples, bool stereoBuffer, int32_t sourceAmplitude,
	                       bool* unisonPartBecameInactive, int32_t overallPitchAdjust, bool doOscSync,
	                       uint32_t* oscSyncPos, uint32_t* oscSyncPhaseIncrements, int32_t amplitudeIncrement,
	                       uint32_t* getPhaseIncrements, bool getOutAfterPhaseIncrements, int32_t waveIndexIncrement,
	                       const VoiceConfig& cfg);
	[[nodiscard]] bool areAllUnisonPartsInactive(const VoiceConfig& cfg) const;

private:
	// inline int32_t doFM(uint32_t *carrierPhase, uint32_t* lastShiftedPhase, uint32_t carrierPhaseIncrement, uint32_t
	// phaseShift);
	bool delete_this_voice_{false};

	// The shared per-render scratch buffers, injected by the adapter (Sound::render passes the file-scope
	// spareRenderingBuffer). Set at the top of render(); the render helpers read it instead of reaching the global,
	// so the kernel names no engine-wide global. Each entry is SSI_TX_BUFFER_NUM_SAMPLES wide.
	std::array<int32_t*, 4> spareBuffers_{};

	// Render one unison part of one source, dispatching by oscType to the per-source component (sample / input /
	// dx7 / virtual-analog / wavetable) and folding their outcomes into one uniform result. The single dispatch
	// seam a SourceComponent interface + the engine registry will build on. Called from renderBasicSource's
	// per-unison loop after the shared pitch/pan/sync setup.
	deluge::synth::SourceRenderResult renderSourceComponent(
	    int32_t u, int32_t s, const ControlBlock& controlBlock, int32_t* oscBuffer, int32_t numSamples,
	    bool stereoBuffer, int32_t sourceAmplitude, int32_t amplitudeIncrement, uint32_t phaseIncrement, bool doOscSync,
	    uint32_t* oscSyncPos, uint32_t* oscSyncPhaseIncrements, int32_t waveIndexIncrement, bool stereoUnison,
	    int32_t amplitudeL, int32_t amplitudeR, const VoiceConfig& cfg, const deluge::synth::VoiceBlock& block);
	void setupPorta(int32_t lastNoteCode);
	int32_t combineExpressionValues(int32_t synthLevelValue, int32_t expressionDimension) const;
};
