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

#include "model/voice/voice.h"
#include "bench/render_profile.h"
#include "definitions_cxx.hpp"
#include "dsp-next/fixed/interpolation/table_interpolation.h" // get_decay4
#include "dsp-next/fixed/shaping/wavefolder.h"
#include "dsp-next/fixed/vca/pan.h"
#include "io/debug/log.h"
#include "memory/stack_guard.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/voice/source_component_render.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/render_scratch.h"
#include "processing/sound/sound.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/multi_range/multisample_range.h"
#include "util/synth_noise.h"

#include "dsp-next/fixed/tables/decay_small.h"
#include "util/misc.h"
#include <cstring>
#include <new>
#include <optional>

using namespace deluge;
using deluge::dsp::fixed::get_decay4;
namespace params = deluge::modulation::params;

PLACE_INTERNAL_FRUNK int32_t spareRenderingBuffer[deluge::render::kSpareRenderingRows][SSI_TX_BUFFER_NUM_SAMPLES]
    __attribute__((aligned(CACHE_LINE_SIZE)));

using namespace deluge::voice;

// Hopefully I could make this use the spareRenderingBuffer instead...

const Patcher::Config kPatcherConfigForVoice = {
    .firstParam = 0,
    .firstNonVolumeParam = params::FIRST_LOCAL_NON_VOLUME,
    .firstHybridParam = params::FIRST_LOCAL__HYBRID,
    .firstExpParam = params::FIRST_LOCAL_EXP,
    .endParams = params::FIRST_GLOBAL,
    .globality = GLOBALITY_LOCAL,
};

int32_t Voice::combineExpressionValues(int32_t synthLevelValue, int32_t expressionDimension) const {
	int32_t voiceLevelValue = this->localExpressionSourceValuesBeforeSmoothing[expressionDimension];
	int32_t combinedValue = (synthLevelValue >> 1) + (voiceLevelValue >> 1);
	return lshift_and_saturate<1>(combinedValue);
}

Voice::Voice(Sound& sound)
    : patcher(kPatcherConfigForVoice, params.sourceValues, params.paramFinalValues), sound{sound} {
	for (int32_t u = 0; u < kMaxNumVoicesUnison; u++) {
		for (int32_t s = 0; s < kNumSources; s++) {
			unisonParts[u].sources[s].bind(ports_[u][s]);
		}
	}
}

// Unusually, modelStack may be supplied as NULL, because when unassigning all voices e.g. on song swap, we won't have
// it. You'll normally want to call audioDriver.voiceUnassigned() after this.
void Voice::setAsUnassigned(ModelStackWithSoundFlags* modelStack, bool deletingSong) {

	unassignStuff(deletingSong);
	delete_this_voice_ = true;
	if (!deletingSong) {
		this->sound.voiceUnassigned(modelStack);
	}
}

void Voice::unassignStuff(bool deletingSong) {
	for (int32_t s = 0; s < kNumSources; s++) {
		for (int32_t u = 0; u < this->sound.snapshot().numUnison; u++) {
			unisonParts[u].sources[s].unassign(deletingSong);
		}
	}
}

// Returns false if the note cannot play and the voice must be unassigned again
bool Voice::note_on(const deluge::synth::NoteSetup& setup, const SoundSnapshot& snapshot,
                    const deluge::synth::NoteOnEvent& event, deluge::synth::VoiceClock clock,
                    size_t num_active_voices) {
	inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] = event.note_before_arp;
	inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)] = event.midi_channel;
	noteCodeAfterArpeggiation = event.note_after_arp;
	overrideAmplitudeEnvelopeReleaseRate = 0;

	if (event.note_after_arp >= 128) {
		params.sourceValues[util::to_underlying(PatchSource::NOTE)] = 2147483647;
	}
	else if (event.note_after_arp <= 0) {
		params.sourceValues[util::to_underlying(PatchSource::NOTE)] = -2147483648;
	}
	else {
		params.sourceValues[util::to_underlying(PatchSource::NOTE)] = ((int32_t)event.note_after_arp - 64) * 33554432;
	}

	// Setup "half-baked" envelope output values. These need to exist before we do the initial patching below - and it's
	// only after that that we can render the "actual" envelope output values, taking their own input patching into
	// account.
	for (int32_t e = 0; e < kNumEnvelopes; e++) {
		envelopes[e].setOutputLevel(setup.attack_is_zero[e] ? 2147483647 : 0);
	}

	// Setup and render local LFO
	lfo2.setLocalInitialPhase(snapshot.lfoConfig[LFO2_ID]);
	params.sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_1)] =
	    lfo2.render(0, snapshot.lfoConfig[LFO2_ID], 0, deluge::synth_noise);
	lfo4.setLocalInitialPhase(snapshot.lfoConfig[LFO4_ID]);
	params.sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_2)] =
	    lfo4.render(0, snapshot.lfoConfig[LFO4_ID], 0, deluge::synth_noise);

	// Setup some sources which won't change for the duration of this note
	params.sourceValues[util::to_underlying(PatchSource::VELOCITY)] =
	    (event.velocity == 128) ? 2147483647 : ((int32_t)event.velocity - 64) * 33554432;

	// "Random" source
	params.sourceValues[util::to_underlying(PatchSource::RANDOM)] = getNoise();

	for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
		localExpressionSourceValuesBeforeSmoothing[m] = event.mpe[m] << 16;
		params.sourceValues[util::to_underlying(PatchSource::X) + m] =
		    combineExpressionValues(setup.monophonic_expression[m], m);
	}

	if (event.reset_envelopes) {
		sourceAmplitudesLastTime.fill(0);
		fmEngine.modulatorAmplitudeLastTime.fill(0);
		overallOscAmplitudeLastTime = 0;
		doneFirstRender = false;
		filterSet.reset();

		saturation_.reset();
	}

	if (setup.porta) [[unlikely]] {
		setupPorta(setup.last_note_code);
	}
	else {
		portaEnvelopePos = 0xFFFFFFFF; // No porta
	}

	// Patch all sources to exclusive params, to give them an initial value. Exclusive params (params with just 1
	// source) aren't continuously recalculated, so they need that initial value. Remember, calculating that initial
	// value also takes into account the "preset value". This probably isn't strictly necessary for sources which we
	// know will be constantly changing, because that would make patching constantly calculate too. But that's only
	// really the envelopes, plus the LFOs (just the local one?) if they're not square
	std::ranges::copy(setup.global_source_values, std::begin(params.sourceValues));
	patcher.performInitialPatching(setup.initial_patching);

	// Setup and render envelopes - again. Because they're local params (since mid-late 2017), we really need to render
	// them *after* initial patching is performed.
	for (int32_t e = 0; e < kNumEnvelopes; e++) {
		int32_t attack = params.paramFinalValues[params::LOCAL_ENV_0_ATTACK + e];
		int32_t sustain = params.paramFinalValues[params::LOCAL_ENV_0_SUSTAIN + e];
		bool directlyToDecay = (attack > 245632); // Skip a (near-)instant attack straight to decay.
		params.sourceValues[util::to_underlying(PatchSource::ENVELOPE_0) + e] =
		    envelopes[e].noteOn(directlyToDecay, sustain);
	}
	stampEnvelope0StageEntry(clock);

	if (event.reset_envelopes) {
		for (int32_t s = 0; s < kNumSources; s++) {
			sourceWaveIndexesLastTime[s] = params.paramFinalValues[params::LOCAL_OSC_A_WAVE_INDEX + s];
		}
	}

	// Each source is active in every unison part if it can sound. In ring-mod, a source that cannot play fails the
	// note.
	for (int32_t s = 0; s < kNumSources; s++) {
		bool sourceEverActive = setup.sources[s].ever_active;
		if (!sourceEverActive || !setup.sources[s].playable) {
			if (snapshot.synthMode == SynthMode::RINGMOD) {
				return false;
			}
			sourceEverActive = false;
		}
		for (int32_t u = 0; u < snapshot.numUnison; u++) {
			unisonParts[u].sources[s].active = sourceEverActive;
		}
	}

	calculate_phase_increments(setup, snapshot);

	const auto priorityRating = static_cast<int32_t>(priority_rating(snapshot.voicePriority, num_active_voices));
	for (int32_t s = 0; s < kNumSources; s++) {
		if (!setup.sources[s].ever_active) [[unlikely]] {
			continue;
		}

		for (int32_t u = 0; u < snapshot.numUnison; u++) {

			// Check that we already marked this unison-part-source as active. Among other things, this ensures that if
			// the osc is set to SAMPLE, there actually is a sample loaded.
			if (unisonParts[u].sources[s].active) {
				bool success = unisonParts[u].sources[s].noteOn(
				    setup.sources[s], event.samples_late, snapshot.oscRetriggerPhase[s], event.reset_envelopes,
				    snapshot.synthMode, noteCodeAfterArpeggiation, event.velocity, priorityRating);
				if (!success) [[unlikely]] {
					return false; // This shouldn't really ever happen I don't think really...
				}
			}
		}
	}

	if (snapshot.synthMode == SynthMode::FM) {
		uint32_t initialPhase = deluge::synth::va_shape::initial_phase_for_zero(OscType::SINE);
		for (int32_t u = 0; u < snapshot.numUnison; u++) {
			for (int32_t m = 0; m < kNumModulators; m++) {
				if (snapshot.modulatorRetriggerPhase[m] != 0xFFFFFFFF) {
					fmEngine.modulatorPhase[u][m] = initialPhase + snapshot.modulatorRetriggerPhase[m];
				}
				if (event.reset_envelopes) {
					fmEngine.modulatorFeedback[u][m] = 0;
				}
			}
		}
	}

	previouslyIgnoredNoteOff = false;
	expressionSourcesCurrentlySmoothing.reset();
	filterGainLastTime = 0;

	return true;
}

void Voice::expressionEventImmediate(int32_t monophonicValue, int32_t voiceLevelValue, int32_t s) {
	int32_t expressionDimension = s - util::to_underlying(PatchSource::X);
	localExpressionSourceValuesBeforeSmoothing[expressionDimension] = voiceLevelValue;
	expressionSourcesFinalValueChanged[expressionDimension] = true;

	params.sourceValues[s] = combineExpressionValues(monophonicValue, expressionDimension);
}

void Voice::expressionEventSmooth(int32_t newValue, int32_t s) {
	int32_t expressionDimension = s - util::to_underlying(PatchSource::X);
	localExpressionSourceValuesBeforeSmoothing[expressionDimension] = newValue;
	expressionSourcesCurrentlySmoothing[expressionDimension] = true;
}

void Voice::change_note(const deluge::synth::NoteSetup& setup, const SoundSnapshot& snapshot,
                        int32_t noteBeforeArpeggiation, int32_t noteAfterArpeggiation, int32_t midiChannel,
                        std::span<const int16_t, kNumExpressionDimensions> mpe) {
	inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] = noteBeforeArpeggiation;
	inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)] = midiChannel;
	noteCodeAfterArpeggiation = noteAfterArpeggiation;

	// We definitely want to go to these values smoothly. Probably wish it was even smoother... Actually nah this sounds
	// / feels great!
	for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
		localExpressionSourceValuesBeforeSmoothing[m] = mpe[m] << 16;
		// TODO: what if there's just channel aftertouch, and it's still held down...
	}
	expressionSourcesCurrentlySmoothing = 0b111;

	if (setup.porta) {
		setupPorta(setup.last_note_code);
	}

	calculate_phase_increments(setup, snapshot);
}

void Voice::setupPorta(int32_t lastNoteCode) {
	portaEnvelopePos = 0;
	int32_t semitoneAdjustment = lastNoteCode - noteCodeAfterArpeggiation;

	int32_t noteWithinOctave = (semitoneAdjustment + 120) % 12;
	int32_t octave = (semitoneAdjustment + 120) / 12;

	int32_t phaseIncrement = noteIntervalTable[noteWithinOctave];

	int32_t shiftRightAmount = 16 - octave;
	if (shiftRightAmount >= 0) {
		phaseIncrement >>= shiftRightAmount;
	}
	else {
		phaseIncrement = 2147483647;
	}

	portaEnvelopeMaxAmplitude = phaseIncrement - kMaxSampleValue;
}

void Voice::randomize_osc_phases(const SoundSnapshot& snapshot) {
	for (int32_t u = 0; u < snapshot.numUnison; u++) {
		for (int32_t s = 0; s < kNumSources; s++) {
			unisonParts[u].sources[s].oscPos = getNoise();
			// TODO: we should do sample play pos, too
		}
		if (snapshot.synthMode == SynthMode::FM) {
			for (int32_t m = 0; m < kNumModulators; m++) {
				fmEngine.modulatorPhase[u][m] = getNoise();
			}
		}
	}
}

void Voice::calculate_phase_increments(const deluge::synth::NoteSetup& setup, const SoundSnapshot& snapshot) {

	int32_t noteCodeWithMasterTranspose = noteCodeAfterArpeggiation + snapshot.transpose;

	for (int32_t s = 0; s < kNumSources; s++) {
		const deluge::synth::SourceSetup& source = setup.sources[s];

		if (!source.ever_active) { // Sets all unison parts inactive by default

makeInactive: // Frequency too high to render! (Higher than 22.05kHz)
			for (int32_t u = 0; u < snapshot.numUnison; u++) {
				unisonParts[u].sources[s].active = false;
			}
			continue;
		}

		int32_t transposedNoteCode = noteCodeWithMasterTranspose + source.transpose;

		uint32_t phaseIncrement;

		// Sample-osc
		if (snapshot.synthMode != SynthMode::FM
		    && (source.osc_type == OscType::SAMPLE || source.osc_type == OscType::INPUT_L
		        || source.osc_type == OscType::INPUT_R || source.osc_type == OscType::INPUT_STEREO)) {

			int32_t noteWithinOctave = (uint16_t)(transposedNoteCode + 240) % 12;
			int32_t octave = (uint16_t)(transposedNoteCode + 120) / 12;

			phaseIncrement =
			    multiply_32x32_rshift32(noteIntervalTable[noteWithinOctave], source.neutral_phase_increment);

			int32_t shiftRightAmount = 13 - octave;

			// If shifting right...
			if (shiftRightAmount >= 0) {
				phaseIncrement >>= shiftRightAmount;
			}

			// If shifting left...
			else {
				int32_t shiftLeftAmount = 0 - shiftRightAmount;

				// If frequency would end up too high...
				// (which means one semitone below the limit, because osc-cent + unison could push it up a semitone)
				if (phaseIncrement >= (2026954652 >> shiftLeftAmount)) {
					goto makeInactive;
				}

				// Or if it's fine...
				else {
					phaseIncrement <<= (shiftLeftAmount);
				}
			}
		}

		// Regular wave osc
		else {
			int32_t noteWithinOctave = (uint16_t)(transposedNoteCode + 240 - 4) % 12;
			int32_t octave = (transposedNoteCode + 120 - 4) / 12;

			int32_t shiftRightAmount = 20 - octave;
			if (shiftRightAmount >= 0) {
				phaseIncrement = noteFrequencyTable[noteWithinOctave] >> shiftRightAmount;
			}

			else {
				goto makeInactive;
			}
		}

		// Cents
		phaseIncrement = source.fine_tuner.detune(phaseIncrement);
		phaseIncrement = snapshot.fineTuner.detune(phaseIncrement); // master transpose

		// If only one unison
		if (snapshot.numUnison == 1) {
			unisonParts[0].sources[s].phaseIncrementStoredValue = phaseIncrement;
		}

		// Or if multiple unison
		else {
			for (int32_t u = 0; u < snapshot.numUnison; u++) {
				unisonParts[u].sources[s].phaseIncrementStoredValue = snapshot.unisonDetuners[u].detune(phaseIncrement);
			}
		}
	}

	// FM modulators
	if (snapshot.synthMode == SynthMode::FM) {
		for (int32_t m = 0; m < kNumModulators; m++) {

			if (!setup.modulator_audible[m]) {
				continue; // Only if modulator active
			}

			int32_t transposedNoteCode = noteCodeWithMasterTranspose + snapshot.modulatorTranspose[m];
			int32_t noteWithinOctave = (transposedNoteCode + 120 - 4) % 12;
			int32_t octave = (transposedNoteCode + 120 - 4) / 12;
			int32_t shiftRightAmount = 20 - octave;

			int32_t phaseIncrement;

			if (shiftRightAmount >= 0) {
				phaseIncrement = noteFrequencyTable[noteWithinOctave] >> shiftRightAmount;
			}

			else {
				// Frequency too high to render! (Higher than 22.05kHz)
				for (int32_t u = 0; u < snapshot.numUnison; u++) {
					fmEngine.modulatorPhaseIncrement[u][m] = 0xFFFFFFFF; // Means "inactive"
				}
				continue;
			}

			// Cents
			phaseIncrement = snapshot.modulatorTransposers[m].detune(phaseIncrement);
			phaseIncrement = snapshot.fineTuner.detune(phaseIncrement); // master transpose

			// If only one unison
			if (snapshot.numUnison == 1) {
				fmEngine.modulatorPhaseIncrement[0][m] = phaseIncrement;
			}

			// Or if multiple unison
			else {
				for (int32_t u = 0; u < snapshot.numUnison; u++) {
					fmEngine.modulatorPhaseIncrement[u][m] = snapshot.unisonDetuners[u].detune(phaseIncrement);
				}
			}
		}
	}
}

void Voice::note_off(const deluge::synth::ReleaseInfo& release, bool allowReleaseStage,
                     deluge::synth::VoiceClock clock) {

	// Only do it if note-offs are meant to be processed for this sound. Otherwise ignore it.
	if (release.note_tails_allowed) {

		// If no release-stage, we'll stop as soon as we can
		if (!allowReleaseStage || !hasReleaseStage()) {
			envelopes[0].unconditionalRelease(Envelope::Stage::FAST_RELEASE);
		}

		// Or, do the release-stage
		else {
			envelopes[0].noteOff(release.envelope_has_sustain[0]);

			// Only start releasing envelope 2 if release wasn't at max value. LOCAL_ENV_1_RELEASE is a
			// *local* param, so read the voice's own params.paramFinalValues (sized LOCAL_LAST) — not
			// sound.paramFinalValues, which holds *global* params only (indexing it here was an
			// out-of-bounds read of adjacent Sound memory; the hardened host build's bounds check caught it).
			if (params.paramFinalValues[params::LOCAL_ENV_1_RELEASE] >= 9) {
				envelopes[1].noteOff(release.envelope_has_sustain[1]);
			}
		}
		stampEnvelope0StageEntry(clock);
	}

	else {
		previouslyIgnoredNoteOff = true;
	}

	if (release.synth_mode != SynthMode::FM) {
		const auto priorityRating =
		    static_cast<int32_t>(priority_rating(release.voice_priority, release.num_active_voices));
		for (int32_t s = 0; s < kNumSources; s++) {
			if (release.osc_types[s] == OscType::SAMPLE) {
				for (int32_t u = 0; u < release.num_unison; u++) {
					if (unisonParts[u].sources[s].active
					    && !unisonParts[u].sources[s].sampleSource.port()->note_off_at_loop_end(priorityRating)) {
						unisonParts[u].sources[s].unassign(false);
					}
				}
			}
			else if (release.osc_types[s] == OscType::DX7) {
				for (int u = 0; u < release.num_unison; u++) {
					unisonParts[u].sources[s].dx7Source.keyup();
				}
			}
		}
	}
}

uint32_t Voice::getLocalLFOPhaseIncrement(const LFOConfig& config, deluge::modulation::params::Local param,
                                          uint32_t syncedPhaseIncrement) {
	if (config.syncLevel == SYNC_LEVEL_NONE) {
		return params.paramFinalValues[param];
	}
	return syncedPhaseIncrement;
}

// Returns false if became inactive and needs unassigning
[[gnu::hot]] bool Voice::render(const ControlBlock& controlBlock, int32_t* soundBuffer, int32_t numSamples,
                                bool soundRenderingInStereo, bool applyingPanAtVoiceLevel, uint32_t sourcesChanged,
                                int32_t externalPitchAdjust, const VoiceConfig& cfg,
                                const deluge::synth::VoiceBlock& block, std::array<int32_t*, 4> spareBuffers) {
	spareBuffers_ = spareBuffers; // the render helpers read these instead of the file-scope spareRenderingBuffer
	// we spread out over a render cycle - allocating and starting the voice takes more time than rendering it so this
	// avoids the cpu spike at note on
	if (justCreated == false) {
		justCreated = true;
		return true;
	}
	checkStack("Voice::render");

	bool didStereoTempBuffer = false;

	// Do envelopes - if they're patched to something (always do the first one though)
	for (int32_t e = 0; e < kNumEnvelopes; e++) {
		if (e == 0 || (cfg.localSourcesPatchedToAnything & (1 << (util::to_underlying(PatchSource::ENVELOPE_0) + e)))) {
			int32_t old = params.sourceValues[util::to_underlying(PatchSource::ENVELOPE_0) + e];
			int32_t release = params.paramFinalValues[params::LOCAL_ENV_0_RELEASE + e];
			if (e == 0 && overrideAmplitudeEnvelopeReleaseRate) {
				release = overrideAmplitudeEnvelopeReleaseRate;
			}
			params.sourceValues[util::to_underlying(PatchSource::ENVELOPE_0) + e] =
			    envelopes[e].render(numSamples, params.paramFinalValues[params::LOCAL_ENV_0_ATTACK + e],
			                        params.paramFinalValues[params::LOCAL_ENV_0_DECAY + e],
			                        params.paramFinalValues[params::LOCAL_ENV_0_SUSTAIN + e], release,
			                        deluge::dsp::fixed::tables::kDecaySmall8.data());
			uint32_t anyChange = (old != params.sourceValues[util::to_underlying(PatchSource::ENVELOPE_0) + e]);
			sourcesChanged |= anyChange << (util::to_underlying(PatchSource::ENVELOPE_0) + e);
		}
	}
	stampEnvelope0StageEntry(block.clock);

	bool unassignVoiceAfter = (envelopes[0].stage() == Envelope::Stage::OFF)
	                          || (envelopes[0].stage() > Envelope::Stage::DECAY
	                              && params.sourceValues[util::to_underlying(PatchSource::ENVELOPE_0)]
	                                     == std::numeric_limits<int32_t>::min());
	// Local LFO
	if (cfg.localSourcesPatchedToAnything & (1 << util::to_underlying(PatchSource::LFO_LOCAL_1))) {
		int32_t old = params.sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_1)];
		// TODO: Same as with LFO1, there should be no reason to recompute the phase increment
		// for every sample.
		params.sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_1)] = lfo2.render(
		    numSamples, cfg.lfo2Config,
		    getLocalLFOPhaseIncrement(cfg.lfo2Config, params::LOCAL_LFO_LOCAL_FREQ_1, cfg.lfo2SyncedPhaseIncrement),
		    deluge::synth_noise);
		uint32_t anyChange = (old != params.sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_1)]);
		sourcesChanged |= anyChange << util::to_underlying(PatchSource::LFO_LOCAL_1);
	}
	if (cfg.localSourcesPatchedToAnything & (1 << util::to_underlying(PatchSource::LFO_LOCAL_2))) {
		int32_t old = params.sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_2)];
		// TODO: Same as with LFO1, there should be no reason to recompute the phase increment
		// for every sample.
		params.sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_2)] = lfo4.render(
		    numSamples, cfg.lfo4Config,
		    getLocalLFOPhaseIncrement(cfg.lfo4Config, params::LOCAL_LFO_LOCAL_FREQ_2, cfg.lfo4SyncedPhaseIncrement),
		    deluge::synth_noise);
		uint32_t anyChange = (old != params.sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_2)]);
		sourcesChanged |= anyChange << util::to_underlying(PatchSource::LFO_LOCAL_2);
	}

	// MPE params

	expressionSourcesCurrentlySmoothing |= cfg.expressionSourcesChangedAtSynthLevel;

	if (expressionSourcesCurrentlySmoothing.any()) {
		expressionSourcesFinalValueChanged |= expressionSourcesCurrentlySmoothing;

		for (int32_t i = 0; i < kNumExpressionDimensions; i++) {
			if (expressionSourcesCurrentlySmoothing[i]) {

				int32_t targetValue = combineExpressionValues(cfg.monophonicExpressionValues[i], i);

				int32_t diff = (targetValue >> 8) - (params.sourceValues[i + util::to_underlying(PatchSource::X)] >> 8);

				if (diff == 0) {
					expressionSourcesCurrentlySmoothing[i] = false;
				}
				else {
					int32_t amountToAdd = diff * numSamples / 4;
					params.sourceValues[i + util::to_underlying(PatchSource::X)] += amountToAdd;
				}
			}
		}
	}

	sourcesChanged |= expressionSourcesFinalValueChanged.to_ulong() << util::to_underlying(PatchSource::X);

	expressionSourcesFinalValueChanged.reset();

	// Patch all the sources to their parameters
	if (sourcesChanged) {
		for (int32_t s = 0; s < util::to_underlying(kFirstLocalSource); s++) {
			params.sourceValues[s] = cfg.globalSourceValues[s];
		}
#if defined(DELUGE_STREAM_BENCH)
		const uint32_t t0 = deluge::bench::render_profile::cycles();
#endif
		patcher.performPatching(sourcesChanged, controlBlock);
#if defined(DELUGE_STREAM_BENCH)
		deluge::bench::render_profile::g_totals.voice_patch += deluge::bench::render_profile::cycles() - t0;
#endif
	}

	// Sort out pitch
	int32_t overallPitchAdjust = params.paramFinalValues[params::LOCAL_PITCH_ADJUST];

	// Pitch adjust from "external" - e.g. the Kit
	if (externalPitchAdjust != kMaxSampleValue) {
		int32_t output = multiply_32x32_rshift32_rounded(overallPitchAdjust, externalPitchAdjust);
		if (output > 8388607) {
			output = 8388607; // Limit it a bit. Not really quite sure if necessary?
		}
		overallPitchAdjust = output << 8;
	}

	// Pitch adjust via MIDI pitch bend and MPE
	int32_t totalBendAmount = // Comes out as a 32-bit number representing a range of +-192 semitones
	    (cfg.monophonicExpressionValues[0] / 192) * cfg.bendRanges[BEND_RANGE_MAIN]
	    + (localExpressionSourceValuesBeforeSmoothing[0] / 192) * cfg.bendRanges[BEND_RANGE_FINGER_LEVEL];

	overallPitchAdjust = getExp(overallPitchAdjust, totalBendAmount >> 1);

	// Porta
	if (portaEnvelopePos < 8388608) {
		int32_t envValue = get_decay4(portaEnvelopePos, 23);
		int32_t pitchAdjustmentHere =
		    kMaxSampleValue + (multiply_32x32_rshift32_rounded(envValue, portaEnvelopeMaxAmplitude) << 1);

		int32_t a = multiply_32x32_rshift32_rounded(overallPitchAdjust, pitchAdjustmentHere);
		if (a > 8388607) {
			a = 8388607; // Prevent overflow! Happened to Matt Bates
		}
		overallPitchAdjust = a << 8;

		// Move envelope on. Using the "release rate" lookup table gives by far the best range of speed values
		int32_t envelopeSpeed = lookupReleaseRate(cableToExpParamShortcut(cfg.portamento)) >> 13;
		portaEnvelopePos += envelopeSpeed * numSamples;
	}

	// Decide whether to do an auto-release for sample. Despite this being envelope-related, meaning we'd ideally prefer
	// to do it before patching, we can only do it after because we need to know pitch

	// If not already releasing and some release is set, and no noise-source...
	//@todo - cache this, it never changes
	if (cfg.synthMode != SynthMode::FM && envelopes[0].stage() < Envelope::Stage::RELEASE && hasReleaseStage()
	    && !cfg.noiseVolumeContainsSomething) {

		uint32_t whichSourcesNeedAttention = 0;

		// We only want to do this if all active sources are play-once samples
		// For each source...
		for (int32_t s = 0; s < kNumSources; s++) {
			// If this source isn't enabled, skip it
			if (!cfg.sourceEverActive[s]) {
				continue;
			}

			// If it's not a sample, or it's not a play-once, or it has a loop-end point but we haven't received the
			// note-off, then we don't want the auto-release feature for it
			if (cfg.oscType[s] != OscType::SAMPLE
			    || cfg.repeatMode[s]
			           != SampleRepeatMode::ONCE // Don't do it for anything else. STRETCH is too hard to calculate
			    || !block.sample_can_auto_release[s]) {
				goto skipAutoRelease;
			}

			whichSourcesNeedAttention |= (1 << s);
		}

		// If either / both sources need attention...
		// only happens for auto release of play once samples - we probably don't get here
		if (whichSourcesNeedAttention) [[unlikely]] {

			int32_t releaseStageLengthSamples =
			    (uint32_t)8388608 / (uint32_t)params.paramFinalValues[params::LOCAL_ENV_0_RELEASE];

			int32_t highestNumSamplesLeft = 0;

			// For each source...
			for (int32_t s = 0; s < kNumSources; s++) {

				// If it needed attention...
				if (whichSourcesNeedAttention & (1 << s)) {

					// This Source needs an auto release applied. Calculate for the last unison, because that'll have
					// the higher pitch, so will be ending soonest
					VoiceUnisonPartSource* voiceUnisonPartSource = &unisonParts[cfg.numUnison - 1].sources[s];
					if (!voiceUnisonPartSource->active) {
						continue;
					}

					const deluge::synth::SamplePlaybackPort& port = *voiceUnisonPartSource->sampleSource.port();
					int32_t bytesLeft = port.bytes_left_to_end();
					int32_t bytesPerSample = port.bytes_per_frame();

					int32_t releaseStageLengthBytes = releaseStageLengthSamples * bytesPerSample;

					// Work out the actual sample read rate, from the "native" read rate for the last unison, combined
					// with the "pitch adjust" amount, and the pitch adjust for this source alone. If the pitch goes
					// crazy-high, this will fall through and prevent auto-release from happening
					uint32_t actualSampleReadRate = voiceUnisonPartSource->phaseIncrementStoredValue;
					if (!adjustPitch(actualSampleReadRate, overallPitchAdjust)) {
						continue;
					}
					if (!adjustPitch(actualSampleReadRate,
					                 params.paramFinalValues[params::LOCAL_OSC_A_PITCH_ADJUST + s])) {
						continue;
					}
					// TODO: actualSampleReadRate should probably be affected by time stretching, too. BUT that'd stuff
					// up some existing users' songs - e.g. Michael B's one I tried during V3.0 beta phase, July 2019

					// Scale that according to our resampling rate
					if (actualSampleReadRate != kMaxSampleValue) {
						releaseStageLengthBytes = ((int64_t)releaseStageLengthBytes * actualSampleReadRate) >> 24;
					}

					// If this sample says it's not time to do auto-release yet, then we don't want to do it yet, so get
					// out
					if (bytesLeft >= releaseStageLengthBytes) {
						goto skipAutoRelease;
					}

					// And also see how many audio samples were left for this source. Only do this in here because it
					// involves time-consuming division
					int32_t samplesLeft = bytesLeft / (bytesPerSample);

					// Scale that according to our resampling rate
					if (actualSampleReadRate != kMaxSampleValue) {
						samplesLeft = (((int64_t)samplesLeft << 24) / actualSampleReadRate);
					}

					highestNumSamplesLeft = std::max(highestNumSamplesLeft, samplesLeft);
				}
			}

			// If we're still here, then *all* sources which needed attention say yes do it now, so do it
			// Just do this for the amplitude envelope
			overrideAmplitudeEnvelopeReleaseRate = 8388608 / highestNumSamplesLeft;
			if (envelopes[0].isAtAttackStart()) {
				envelopes[0].setOutputLevel(2147483647);
			}
			envelopes[0].unconditionalRelease();
			stampEnvelope0StageEntry(block.clock);
		}
	}
skipAutoRelease: {}

	if (!doneFirstRender && params.paramFinalValues[params::LOCAL_ENV_0_ATTACK] > 245632) {
		for (int32_t m = 0; m < kNumModulators; m++) {
			fmEngine.modulatorAmplitudeLastTime[m] = params.paramFinalValues[params::LOCAL_MODULATOR_0_VOLUME + m];
		}
	}

	// Apply envelope 0 to volume. This takes effect as a cut only; when the envelope is at max height, volume is
	// unaffected. Important that we use lshift_and_saturate here - otherwise, number can overflow if combining high
	// velocity patching with big LFO
	int32_t overallOscAmplitude = lshift_and_saturate<2>(
	    multiply_32x32_rshift32(params.paramFinalValues[params::LOCAL_VOLUME],
	                            (params.sourceValues[util::to_underlying(PatchSource::ENVELOPE_0)] >> 1) + 1073741824));

	// This is the gain which gets applied to compensate for any change in gain that the filter is going to cause
	int32_t filterGain;

	// Prepare the filters. Level adjustment for unison happens before the filters, so the gain going in is neutral.
	const bool allow_oversampling = cfg.cpu_direness < 14;
	filterGain = filterSet.configure(cfg.volumeNeutralValueForUnison << 1,
	                                 {
	                                     .frequency = params.paramFinalValues[params::LOCAL_LPF_FREQ],
	                                     .resonance = params.paramFinalValues[params::LOCAL_LPF_RESONANCE],
	                                     .morph = params.paramFinalValues[params::LOCAL_LPF_MORPH],
	                                     .allow_oversampling = allow_oversampling,
	                                 },
	                                 cfg.low_pass,
	                                 {
	                                     .frequency = params.paramFinalValues[params::LOCAL_HPF_FREQ],
	                                     .resonance = params.paramFinalValues[params::LOCAL_HPF_RESONANCE],
	                                     .morph = params.paramFinalValues[params::LOCAL_HPF_MORPH],
	                                     .allow_oversampling = allow_oversampling,
	                                 },
	                                 cfg.high_pass, cfg.filter_route);

	SynthMode synthMode = cfg.synthMode;

	int32_t sourceAmplitudes[kNumSources];
	int32_t sourceAmplitudeIncrements[kNumSources];

	bool modulatorsActive[kNumModulators];
	int32_t modulatorAmplitudeIncrements[kNumModulators];

	int32_t overallOscillatorAmplitudeIncrement;

	// If not ringmod, then sources need their volume calculated
	if (synthMode != SynthMode::RINGMOD) [[likely]] {

		// params::LOCAL_OSC_x_VOLUME can normally only be up to a quarter of full range, but patching can make it up to
		// full-range overallOscAmplitude (same range as) params::LOCAL_VOLUME, is the same.

		// Let's impose a new limit, that only a total of 4x amplification via patching is possible (not 16x).
		// Chances are, the user won't even need that much, let alone would have the osc volume *and* the synth master
		// volume on full We then have space to make each osc's amplitude 4x what it could have been otherwise

		// If FM, we work the overall amplitude into each oscillator's, to avoid having to do an extra multiplication
		// for every audio sample at the end
		if (synthMode == SynthMode::FM) {

			// Apply compensation for unison
			overallOscAmplitude = multiply_32x32_rshift32_rounded(overallOscAmplitude, cfg.volumeNeutralValueForUnison)
			                      << 3;

			int32_t a =
			    multiply_32x32_rshift32(params.paramFinalValues[params::LOCAL_OSC_A_VOLUME], overallOscAmplitude);
			int32_t b =
			    multiply_32x32_rshift32(params.paramFinalValues[params::LOCAL_OSC_B_VOLUME], overallOscAmplitude);

			// Clip off those amplitudes before they get too high. I think these were originally intended to stop the
			// amplitude rising to more than "4", whatever that meant?
			sourceAmplitudes[0] = std::min(a, (int32_t)134217727);
			sourceAmplitudes[1] = std::min(b, (int32_t)134217727);
		}

		// Or if subtractive, we don't do that, because we do want to apply the overall amplitude *after* the filter
		else {
			if (cfg.hasFilters) {
				sourceAmplitudes[0] =
				    multiply_32x32_rshift32_rounded(params.paramFinalValues[params::LOCAL_OSC_A_VOLUME], filterGain);
				sourceAmplitudes[1] =
				    multiply_32x32_rshift32_rounded(params.paramFinalValues[params::LOCAL_OSC_B_VOLUME], filterGain);
			}
			else {
				sourceAmplitudes[0] = params.paramFinalValues[params::LOCAL_OSC_A_VOLUME] >> 4;
				sourceAmplitudes[1] = params.paramFinalValues[params::LOCAL_OSC_B_VOLUME] >> 4;
			}
		}

		bool shouldAvoidIncrementing = doneFirstRender ? (filterGainLastTime != filterGain)
		                                               : (params.paramFinalValues[params::LOCAL_ENV_0_ATTACK] > 245632);

		if (shouldAvoidIncrementing) {
			for (int32_t s = 0; s < kNumSources; s++) {
				sourceAmplitudesLastTime[s] = sourceAmplitudes[s];
			}
		}

		for (int32_t s = 0; s < kNumSources; s++) {
			sourceAmplitudeIncrements[s] = (int32_t)(sourceAmplitudes[s] - sourceAmplitudesLastTime[s]) / numSamples;
		}

		filterGainLastTime = filterGain;

		// If FM, cache whether modulators are active
		if (synthMode == SynthMode::FM) {
			for (int32_t m = 0; m < kNumModulators; m++) {
				modulatorsActive[m] = (params.paramFinalValues[params::LOCAL_MODULATOR_0_VOLUME + m] != 0
				                       || fmEngine.modulatorAmplitudeLastTime[m] != 0);

				if (modulatorsActive[m]) {
					modulatorAmplitudeIncrements[m] =
					    (int32_t)(params.paramFinalValues[params::LOCAL_MODULATOR_0_VOLUME + m]
					              - fmEngine.modulatorAmplitudeLastTime[m])
					    / numSamples;
				}
			}
		}
	}

	int32_t sourceWaveIndexIncrements[kNumSources];

	if (synthMode != SynthMode::FM) {
		if (!doneFirstRender && params.paramFinalValues[params::LOCAL_ENV_0_ATTACK] > 245632) {
			overallOscAmplitudeLastTime = overallOscAmplitude;
		}
		overallOscillatorAmplitudeIncrement = (int32_t)(overallOscAmplitude - overallOscAmplitudeLastTime) / numSamples;

		for (int32_t s = 0; s < kNumSources; s++) {
			sourceWaveIndexIncrements[s] =
			    (int32_t)(params.paramFinalValues[params::LOCAL_OSC_A_WAVE_INDEX + s] - sourceWaveIndexesLastTime[s])
			    / numSamples;
		}
	}

	doneFirstRender = true;

	uint32_t oscSyncPos[kMaxNumVoicesUnison];
	bool doingOscSync = cfg.doingOscSync;

	// Oscillator sync: capture the master (source 0) phase at block start — from whichever persistent engine owns
	// it (virtual-analog / wavetable), else oscPos.
	if (doingOscSync) {
		for (int32_t u = 0; u < cfg.numUnison; u++) {
			oscSyncPos[u] = unisonParts[u].sources[0].currentOscPhase();
		}
	}

	// whether stereo unison actually is active. if stereo is being vetoed from higher up, don't do it.
	bool stereoUnison = cfg.unisonStereoSpread && cfg.numUnison > 1 && soundRenderingInStereo;

	int32_t* oscBuffer;
	bool anythingInOscBuffer = false;
	int32_t sourceAmplitudesNow[kNumSources];
	for (int32_t s = 0; s < kNumSources; s++) {
		sourceAmplitudesNow[s] = sourceAmplitudesLastTime[s];
	}

	int32_t amplitudeL, amplitudeR;
	bool doPanning;

	// two first indicies are reserved in case we need stereo for unison spread
	oscBuffer = spareBuffers_[0];
	int32_t channels = stereoUnison ? 2 : 1;

	int32_t const* const oscBufferEnd = oscBuffer + numSamples * channels;

	// If any noise, do that. By cutting a corner here, we do it just once for all "unison", rather than for each
	// unison. Increasing number of unison cuts the volume of the oscillators
	if (params.paramFinalValues[params::LOCAL_NOISE_VOLUME] != 0 && synthMode != SynthMode::FM) [[unlikely]] {

		// This was >>2, but because I had a bug in V2.0.x which made noise too loud if filter on,
		// I'm now making this louder to compensate and remain consistent by going just >>1.
		// So now I really need to make it so that sounds made before V2.0 halve their noise volume... (Hey, did I
		// ever do this? Who knows...)
		int32_t n = params.paramFinalValues[params::LOCAL_NOISE_VOLUME] >> 1;
		if (cfg.hasFilters) {
			n = multiply_32x32_rshift32(n, filterGain) << 4;
		}

		// Perform the same limiting that we do above for the oscillators
		int32_t noiseAmplitude = std::min(n, (int32_t)268435455) >> 2;

		int32_t* __restrict__ thisSample = oscBuffer;
		do {
			*thisSample = multiply_32x32_rshift32(getNoise(), noiseAmplitude);
			thisSample++;
		} while (thisSample != oscBufferEnd);

		anythingInOscBuffer = true;
	}

	// Otherwise, clear the buffer
	else {
		memset(oscBuffer, 0, channels * numSamples * sizeof(int32_t));
	}

	// Even if first rendering into a local Voice buffer, we'll very often still just do panning at the Sound level
	if (!applyingPanAtVoiceLevel) {
		doPanning = false;
	}
	else {
		// Set up panning
		const int32_t pan = params.paramFinalValues[params::LOCAL_PAN];
		doPanning = false;
		if (cfg.engineRenderingInStereo) {
			const deluge::dsp::fixed::PanAmplitudes amplitudes = deluge::dsp::fixed::pan_amplitudes(pan);
			amplitudeL = amplitudes.left;
			amplitudeR = amplitudes.right;
			doPanning = pan != 0;
		}
	}

	// Normal mode: subtractive / samples. The engine owns the per-source render loop + osc-sync + the stereo
	// buffer dance + the unison-inactive→unassign decision (it delegates each source's render back to
	// renderBasicSource for now).
	if (synthMode == SynthMode::SUBTRACTIVE) {
		subtractiveEngine.render(*this, controlBlock, block, oscBuffer, numSamples, doingOscSync, oscSyncPos,
		                         overallPitchAdjust, sourceAmplitudesNow, sourceAmplitudeIncrements,
		                         sourceWaveIndexIncrements, anythingInOscBuffer, didStereoTempBuffer,
		                         unassignVoiceAfter, cfg);
	}

	// Ring-mod: the engine owns the whole per-voice render (unison loop + per-source pitch math + the combine),
	// mirroring SubtractiveEngine. The per-voice pre-roll is passed in.
	else if (synthMode == SynthMode::RINGMOD) {
		ringModEngine.render(*this, oscBuffer, numSamples, doingOscSync, oscSyncPos, overallPitchAdjust,
		                     sourceWaveIndexIncrements, stereoUnison, filterGain, didStereoTempBuffer, spareBuffers_[2],
		                     spareBuffers_[3], cfg, block);
	}

	// FM: the engine owns the whole per-voice render (unison loop + per-source carrier pitch math + the modulator/
	// carrier topology), mirroring SubtractiveEngine / RingModEngine. The per-voice pre-roll is passed in.
	else {
		fmEngine.render(*this, numSamples, overallPitchAdjust, stereoUnison, sourceAmplitudes, sourceAmplitudesNow,
		                sourceAmplitudeIncrements, modulatorsActive, modulatorAmplitudeIncrements, oscBuffer,
		                spareBuffers_[2], spareBuffers_[3], didStereoTempBuffer, cfg);
	}

	if (didStereoTempBuffer) {
		int32_t* const oscBufferEnd = oscBuffer + (numSamples << 1);
		// fold
		if (params.paramFinalValues[params::LOCAL_FOLD] > 0) {
			const std::span<dsp::q31> stereo{oscBuffer, oscBufferEnd};
			dsp::fixed::Wavefolder{params.paramFinalValues[params::LOCAL_FOLD]}.render_block(stereo, stereo);
		}
		// Filters
		filterSet.render_stereo({oscBuffer, oscBufferEnd}, deluge::render::filter_scratch(), deluge::synth_noise);

		// No clipping
		if (!cfg.clippingAmount) {

			int32_t const* __restrict__ oscBufferPos = oscBuffer; // For traversal
			StereoSample* __restrict__ outputSample = (StereoSample*)soundBuffer;
			int32_t overallOscAmplitudeNow = overallOscAmplitudeLastTime;

			do {
				int32_t outputSampleL = *(oscBufferPos++);
				int32_t outputSampleR = *(oscBufferPos++);

				overallOscAmplitudeNow += overallOscillatorAmplitudeIncrement;
				if (synthMode != SynthMode::FM) {
					outputSampleL = multiply_32x32_rshift32_rounded(outputSampleL, overallOscAmplitudeNow) << 1;
					outputSampleR = multiply_32x32_rshift32_rounded(outputSampleR, overallOscAmplitudeNow) << 1;
				}

				// Write to the output buffer, panning or not
				if (doPanning) {
					*outputSample += deluge::dsp::panned_stereo(outputSampleL, outputSampleR, amplitudeL, amplitudeR);
				}
				else {
					*outputSample += StereoSample{.l = outputSampleL, .r = outputSampleR};
				}

				outputSample++;
			} while (oscBufferPos != oscBufferEnd);
		}

		// Yes clipping
		else {

			int32_t const* __restrict__ oscBufferPos = oscBuffer; // For traversal
			StereoSample* __restrict__ outputSample = (StereoSample*)soundBuffer;
			int32_t overallOscAmplitudeNow = overallOscAmplitudeLastTime;
			const deluge::effects::SaturationParams saturation{.clipping_amount = cfg.clippingAmount,
			                                                   .variant = deluge::effects::SaturationVariant::VOICE};

			do {
				int32_t outputSampleL = *(oscBufferPos++);
				int32_t outputSampleR = *(oscBufferPos++);

				overallOscAmplitudeNow += overallOscillatorAmplitudeIncrement;
				if (synthMode != SynthMode::FM) {
					outputSampleL = multiply_32x32_rshift32_rounded(outputSampleL, overallOscAmplitudeNow) << 1;
					outputSampleR = multiply_32x32_rshift32_rounded(outputSampleR, overallOscAmplitudeNow) << 1;
				}

				outputSampleL = saturation_.saturate(outputSampleL, 0, saturation);
				outputSampleR = saturation_.saturate(outputSampleR, 1, saturation);

				// Write to the output buffer, panning or not
				if (doPanning) {
					*outputSample += deluge::dsp::panned_stereo(outputSampleL, outputSampleR, amplitudeL, amplitudeR);
				}
				else {
					*outputSample += StereoSample{.l = outputSampleL, .r = outputSampleR};
				}

				outputSample++;
			} while (oscBufferPos != oscBufferEnd);
		}
	}
	else {
		/*
		do {
		    int32_t distanceToGoL = *oscBufferPos - hpfMem;
		    hpfMem += distanceToGoL >> 11;
		    *oscBufferPos -= hpfMem;

		} while (++oscBufferPos != oscBufferEnd);

		oscBufferPos = oscBuffer;
		*/

		int32_t* const oscBufferEnd = oscBuffer + numSamples;
		// wavefolding pre filter
		if (params.paramFinalValues[params::LOCAL_FOLD] > 0) {
			const std::span<dsp::q31> mono{oscBuffer, oscBufferEnd};
			dsp::fixed::Wavefolder{params.paramFinalValues[params::LOCAL_FOLD]}.render_block(mono, mono);
		}

		filterSet.render_mono({oscBuffer, oscBufferEnd}, deluge::render::filter_scratch(), deluge::synth_noise);

		// No clipping
		if (!cfg.clippingAmount) {
			int32_t const* __restrict__ oscBufferPos = oscBuffer; // For traversal
			int32_t* __restrict__ outputSample = soundBuffer;
			int32_t overallOscAmplitudeNow = overallOscAmplitudeLastTime;

			do {
				int32_t output = *oscBufferPos;

				if (synthMode != SynthMode::FM) {
					overallOscAmplitudeNow += overallOscillatorAmplitudeIncrement;
					output = multiply_32x32_rshift32_rounded(output, overallOscAmplitudeNow) << 1;
				}

				if (soundRenderingInStereo) {
					if (doPanning) {
						*((StereoSample*)outputSample) += deluge::dsp::panned_mono(output, amplitudeL, amplitudeR);
					}
					else {
						*((StereoSample*)outputSample) += StereoSample::from_mono(output);
					}
					outputSample += 2;
				}
				else {
					*outputSample += output;
					outputSample++;
				}
			} while (++oscBufferPos != oscBufferEnd);
		}

		// Yes clipping
		else {
			int32_t const* __restrict__ oscBufferPos = oscBuffer; // For traversal
			int32_t* __restrict__ outputSample = soundBuffer;
			int32_t overallOscAmplitudeNow = overallOscAmplitudeLastTime;

			const deluge::effects::SaturationParams saturation{.clipping_amount = cfg.clippingAmount,
			                                                   .variant = deluge::effects::SaturationVariant::VOICE};
			do {
				int32_t output = *oscBufferPos;

				if (synthMode != SynthMode::FM) {
					overallOscAmplitudeNow += overallOscillatorAmplitudeIncrement;
					output = multiply_32x32_rshift32_rounded(output, overallOscAmplitudeNow) << 1;
				}

				output = saturation_.saturate(output, 0, saturation);

				if (soundRenderingInStereo) {
					if (doPanning) {
						*((StereoSample*)outputSample) += deluge::dsp::panned_mono(output, amplitudeL, amplitudeR);
					}
					else {
						*((StereoSample*)outputSample) += StereoSample::from_mono(output);
					}
					outputSample += 2;
				}
				else {
					*outputSample += output;
					outputSample++;
				}
			} while (++oscBufferPos != oscBufferEnd);
		}
	}

renderingDone:

	for (int32_t s = 0; s < kNumSources; s++) {
		sourceAmplitudesLastTime[s] = sourceAmplitudes[s];
		sourceWaveIndexesLastTime[s] = params.paramFinalValues[params::LOCAL_OSC_A_WAVE_INDEX + s];
	}
	for (int32_t m = 0; m < kNumModulators; m++) {
		fmEngine.modulatorAmplitudeLastTime[m] = params.paramFinalValues[params::LOCAL_MODULATOR_0_VOLUME + m];
	}
	overallOscAmplitudeLastTime = overallOscAmplitude;

	return !unassignVoiceAfter;
}

bool Voice::areAllUnisonPartsInactive(const VoiceConfig& cfg) const {
	// If no noise-source, then it might be time to unassign the voice...
	if (!cfg.noiseVolumeContainsSomething) {

		// See if all unison parts are now inactive
		for (int32_t s = 0; s < kNumSources; s++) {
			if (!cfg.sourceEverActive[s]) {
				continue;
			}
			for (int32_t u = 0; u < cfg.numUnison; u++) {
				if (unisonParts[u].sources[s].active) {
					return false;
				}
			}
		}

		// If here, no parts active anymore!
		return true;
	}

	return false;
}

// Returns false if it takes us above 22.05kHz, in which case it doesn't return a valid value
bool Voice::adjustPitch(uint32_t& phaseIncrement, int32_t adjustment) {
	if (adjustment != kMaxSampleValue) {
		int32_t output = multiply_32x32_rshift32_rounded(phaseIncrement, adjustment);
		if (output >= (1 << 23)) {
			return false;
		}
		phaseIncrement = output << 8;
	}
	return true;
}

// This function renders all unison for a source/oscillator. Amplitude and the incrementing thereof is done
// independently for each unison, despite being the same for all of them, and you might be wondering why this is. Yes in
// the case of an 8-unison sound it'd work out slightly better to apply amplitude to all unison together, but here's why
// this just generally isn't all that advantageous:
//	-	Doing it separately for each unison, as we are, means each unison of *both* sources can just sum itself directly
// onto the buffer for the whole Voice, or in special cases the Sound, 		which only had to be wiped clean once. But
// applying amplitude to the combined unisons of one source would mean we'd need to clear a buffer for that source, then
// render our 		potentially just one unison into that (summing to the buffer's existing contents), then as amplitude
// is applied to the buffer, we'd write the output of that into the 		Voice buffer (summing to its contents too).
// So basically an extra level of summing and clearing would have to happen, in addition to the extra copying which is
// obvious. 	-	And you might be thinking that for oscillator sync this would work well, because it already applies
// amplitude in a separate step after rendering the wave. 		But no - a key part of why this works well as it is is
// that the summing between unisons only happens along with that amplitude-application, after the initial wave render
// has finished chopping around the contents of its buffer. Having that summed into an all-unison buffer would still
// require an additional copying(summing) step, 		and we might as well just apply amplitude while that's
// happening, which is exactly how it is currently.

// Render one unison part of one source, dispatching by oscType to the per-source component and folding the
// per-engine outcomes into one uniform SourceRenderResult. The single uniform dispatch seam a SourceComponent
// interface + the engine registry will build on. (Still a Voice method using public state — this is the seam,
// not yet the de-coupling.)
deluge::synth::SourceRenderResult Voice::renderSourceComponent(
    int32_t u, int32_t s, const ControlBlock& controlBlock, int32_t* oscBuffer, int32_t numSamples, bool stereoBuffer,
    int32_t sourceAmplitude, int32_t amplitudeIncrement, uint32_t phaseIncrement, bool doOscSync, uint32_t* oscSyncPos,
    uint32_t* oscSyncPhaseIncrements, int32_t waveIndexIncrement, bool stereoUnison, int32_t amplitudeL,
    int32_t amplitudeR, const VoiceConfig& cfg, const deluge::synth::VoiceBlock& block) {
	using deluge::synth::SourceRenderResult;
	VoiceUnisonPartSource* voiceUnisonPartSource = &unisonParts[u].sources[s];

	// SAMPLE caching policy: a sample is not cached when a source that can change during the note modulates its pitch.
	// The routing groups each destination's cables, so only the two pitch destinations' cables are scanned. Computed
	// only for SAMPLE, the one source that reads it, and only on a sample's first render.
	bool pitchModDisallowsCache = false;
	if (cfg.oscType[s] == OscType::SAMPLE) {
		const deluge::synth::PatchRouting& routing = controlBlock.routing;
		const deluge::synth::CableTarget overallPitch = deluge::synth::CableTarget::param(params::LOCAL_PITCH_ADJUST);
		const deluge::synth::CableTarget sourcePitch =
		    deluge::synth::CableTarget::param(params::LOCAL_OSC_A_PITCH_ADJUST + s);
		for (const deluge::synth::RoutedDestination* d = routing.destinations[GLOBALITY_LOCAL];
		     d != nullptr && d->sources != 0 && !pitchModDisallowsCache; d++) {
			if (d->target != overallPitch && d->target != sourcePitch) {
				continue;
			}
			for (int32_t c = d->first_cable; c < d->end_cable; c++) {
				const PatchSource from = routing.cables[c].source;
				if (from == PatchSource::ENVELOPE_0 || from == PatchSource::ENVELOPE_1
				    || from == PatchSource::LFO_GLOBAL_1 || from == PatchSource::LFO_LOCAL_1
				    || from == PatchSource::LFO_GLOBAL_2 || from == PatchSource::LFO_LOCAL_2
				    || from == PatchSource::RANDOM
				    || (from == PatchSource::AFTERTOUCH
				        && params.sourceValues[util::to_underlying(PatchSource::AFTERTOUCH)])
				    || (from == PatchSource::SIDECHAIN
				        && cfg.globalSourceValues[util::to_underlying(PatchSource::SIDECHAIN)])) {
					pitchModDisallowsCache = true;
					break;
				}
			}
		}
	}

	// All five sources render through the polymorphic synth::Source seam, which owns (for oscillators) the osc-sync
	// freq-too-high SKIP + mono-scratch/stereo-fold, and (per engine) the block render. The app-side adapter just
	// gathers the already-modulated per-block values into the SourceRenderContext.
	uint32_t oscRetriggerPhase = cfg.oscRetriggerPhase[s]; // Yes we might need this even if not doing osc sync.
	uint32_t oscSyncPosThisUnison = doOscSync ? oscSyncPos[u] : 0;
	uint32_t oscSyncPhaseIncrementsThisUnison = doOscSync ? oscSyncPhaseIncrements[u] : 0;

	// Work out pulse width
	uint32_t pulseWidth =
	    (uint32_t)lshift_and_saturate<1>(params.paramFinalValues[params::LOCAL_OSC_A_PHASE_WIDTH + s]);

	synth::SourceRenderContext ctx{
	    .oscBuffer = oscBuffer,
	    .numSamples = numSamples,
	    .stereoBuffer = stereoBuffer,
	    .stereoUnison = stereoUnison,
	    .amplitudeL = amplitudeL,
	    .amplitudeR = amplitudeR,
	    .sourceAmplitude = sourceAmplitude,
	    .amplitudeIncrement = amplitudeIncrement,
	    .phaseIncrement = phaseIncrement,
	    .doOscSync = doOscSync,
	    .oscSyncPos = oscSyncPosThisUnison,
	    .oscSyncPhaseIncrement = oscSyncPhaseIncrementsThisUnison,
	    .controls = makeBlockControls(sourceAmplitude, amplitudeIncrement, pulseWidth, /*applyAmplitude=*/true,
	                                  doOscSync, oscSyncPosThisUnison, oscSyncPhaseIncrementsThisUnison,
	                                  oscRetriggerPhase, cfg.cpu_direness),
	    .waveTableData = block.wave_tables[s],
	    .waveIndexLast = static_cast<int32_t>(sourceWaveIndexesLastTime[s]),
	    .waveIndexIncrement = waveIndexIncrement,
	    .stereoScratch = spareBuffers_[2],
	    .stereoScratchClearSamples = SSI_TX_BUFFER_NUM_SAMPLES,
	    .syncScratch = deluge::render::osc_sync_scratch().data(),
	    .ampModParam = params.paramFinalValues[params::LOCAL_OSC_A_PHASE_WIDTH + s], // DX7 amplitude-mod control
	    .patchChanged = cfg.dxPatchChanged[s],                                       // DX7
	    .midinote = noteCodeAfterArpeggiation,                                       // DX7
	    .interpolationBufferSize =
	        SampleControls::getInterpolationBufferSize(cfg.interpolationMode[s], phaseIncrement), // live input
	    .portaEnvelopePos = portaEnvelopePos,                                                     // sample
	    .priorityRating = static_cast<int32_t>(priority_rating(cfg.voice_priority, block.num_active_voices)), // sample
	    .phaseIncrementStoredValue = voiceUnisonPartSource->phaseIncrementStoredValue, // sample late-start
	    .pitchModDisallowsCache = pitchModDisallowsCache,                              // sample caching policy
	    .noise = &deluge::synth_noise,                                                 // live-input hop search
	};

	// sample → sampleSource, DX7 → dx7Source, INPUT_* → inputSource, wavetable → wavetableSource, supersaw →
	// supersawSource, else virtual-analog → vaSource — every source is now a persistent synth::Source; the legacy
	// oscType megaswitch is gone.
	synth::Source* component;
	if (cfg.oscType[s] == OscType::SAMPLE) {
		component = &unisonParts[u].sources[s].sampleSource;
	}
	else if (cfg.oscType[s] == OscType::DX7) {
		component = &unisonParts[u].sources[s].dx7Source;
	}
	else if (cfg.oscType[s] == OscType::INPUT_L || cfg.oscType[s] == OscType::INPUT_R
	         || cfg.oscType[s] == OscType::INPUT_STEREO) {
		component = &unisonParts[u].sources[s].inputSource;
	}
	else if (unisonParts[u].sources[s].vaSource.has_value()) {
		component = &*unisonParts[u].sources[s].vaSource;
	}
	else if (unisonParts[u].sources[s].supersawSource.has_value()) {
		component = &*unisonParts[u].sources[s].supersawSource;
	}
	else {
		component = &*unisonParts[u].sources[s].wavetableSource;
	}
	return component->renderSource(ctx);
}

void Voice::renderBasicSource(const ControlBlock& controlBlock, const deluge::synth::VoiceBlock& block, int32_t s,
                              int32_t* __restrict__ oscBuffer, int32_t numSamples, bool stereoBuffer,
                              int32_t sourceAmplitude, bool* __restrict__ unisonPartBecameInactive,
                              int32_t overallPitchAdjust, bool doOscSync, uint32_t* __restrict__ oscSyncPos,
                              uint32_t* __restrict__ oscSyncPhaseIncrements, int32_t amplitudeIncrement,
                              uint32_t* __restrict__ getPhaseIncrements, bool getOutAfterPhaseIncrements,
                              int32_t waveIndexIncrement, const VoiceConfig& cfg) {

	checkStack("Voice::renderBasicSource");

	// For each unison part
	for (int32_t u = 0; u < cfg.numUnison; u++) {

		VoiceUnisonPartSource* voiceUnisonPartSource = &unisonParts[u].sources[s];

		// Samples may become inactive
		if (!voiceUnisonPartSource->active) {
			continue;
		}

		if (false) {
instantUnassign:

#ifdef TEST_SAMPLE_LOOP_POINTS
			FREEZE_WITH_ERROR("YEP");
#endif

			*unisonPartBecameInactive = true;
			voiceUnisonPartSource->unassign(false);
			continue;
		}

		uint32_t phaseIncrement = voiceUnisonPartSource->phaseIncrementStoredValue;

		// Overall pitch adjustment
		if (!adjustPitch(phaseIncrement, overallPitchAdjust)) {

pitchTooHigh:
			if (getPhaseIncrements) {
				getPhaseIncrements[u] = 0;
			}
			continue;
		}

		// Individual source pitch adjustment
		if (!adjustPitch(phaseIncrement, params.paramFinalValues[params::LOCAL_OSC_A_PITCH_ADJUST + s])) {
			goto pitchTooHigh;
		}

		if (getPhaseIncrements) {
			getPhaseIncrements[u] = phaseIncrement;

			if (getOutAfterPhaseIncrements) {
				// Skipped source still advances its phase (it's the osc-sync master) — in whichever engine owns it.
				voiceUnisonPartSource->advanceOscPhase(phaseIncrement * numSamples);
				continue;
			}
		}

		bool stereoUnison = cfg.unisonStereoSpread && cfg.numUnison > 1 && stereoBuffer;
		const deluge::dsp::fixed::PanAmplitudes unisonPanAmplitudes =
		    deluge::dsp::fixed::pan_amplitudes(stereoUnison ? cfg.unisonPan[u] : 0);
		int32_t amplitudeL = unisonPanAmplitudes.left;
		int32_t amplitudeR = unisonPanAmplitudes.right;
		// used if mono source but stereoUnison active

		auto result =
		    renderSourceComponent(u, s, controlBlock, oscBuffer, numSamples, stereoBuffer, sourceAmplitude,
		                          amplitudeIncrement, phaseIncrement, doOscSync, oscSyncPos, oscSyncPhaseIncrements,
		                          waveIndexIncrement, stereoUnison, amplitudeL, amplitudeR, cfg, block);
		if (result == deluge::synth::SourceRenderResult::UNASSIGN) {
			goto instantUnassign;
		}
		if (result == deluge::synth::SourceRenderResult::SKIP) {
			continue;
		}
	}
}

bool Voice::doFastRelease(uint32_t releaseIncrement, deluge::synth::VoiceClock clock) {
	if (doneFirstRender) {
		envelopes[0].unconditionalRelease(Envelope::Stage::FAST_RELEASE, releaseIncrement);
		stampEnvelope0StageEntry(clock);
		return true;
	}

	// Or if first render not done yet, we actually don't want to hear anything at all, so just unassign it
	else {
		return false;
	}
}

bool Voice::speedUpRelease(deluge::synth::VoiceClock clock, uint32_t minFastReleaseIncrement) {
	if (!doneFirstRender) {
		return false;
	}

	auto currentStage = envelopes[0].stage();
	if (currentStage == Envelope::Stage::RELEASE) {
		auto currentRelease = params.paramFinalValues[params::LOCAL_ENV_0_RELEASE];
		envelopes[0].unconditionalRelease(Envelope::Stage::FAST_RELEASE,
		                                  std::max<uint32_t>(currentRelease * 2, minFastReleaseIncrement));
	}
	else if (currentStage == Envelope::Stage::FAST_RELEASE) {
		auto currentRelease = envelopes[0].fastReleaseRate();
		envelopes[0].unconditionalRelease(Envelope::Stage::FAST_RELEASE,
		                                  std::max<uint32_t>(currentRelease * 2, minFastReleaseIncrement));
	}
	// Cull-driven release must not inherit a patch's long musical release, or culled voices can stack up.
	else {
		envelopes[0].unconditionalRelease(Envelope::Stage::FAST_RELEASE, minFastReleaseIncrement);
	}
	stampEnvelope0StageEntry(clock);
	return true;
}

bool Voice::doImmediateRelease(deluge::synth::VoiceClock clock) {
	if (doneFirstRender) {
		envelopes[0].unconditionalOff();
		stampEnvelope0StageEntry(clock);
		return true;
	}

	// Or if first render not done yet, we actually don't want to hear anything at all, so just unassign it
	return false;
}

bool Voice::hasReleaseStage() {
	return (params.paramFinalValues[params::LOCAL_ENV_0_RELEASE] <= 18359);
}

// if more added, need to change the shifts around
static_assert(Envelope::kNumStages < 8, "Too many envelope stages");
static_assert(kNumVoicePriorities < 4, "Too many priority options");

// Higher numbers are lower priority. 1 is top priority. Will never return 0, because the voice clock starts at 1
uint32_t Voice::priority_rating(VoicePriority voice_priority, size_t num_active_voices) const {
	return
	    // Bits 30-31 - manual priority setting
	    ((uint32_t)(3 - util::to_underlying(voice_priority)) << 30)

	    // Bits 27-29 - how many voices that Sound has
	    // - that one really does need to go above state, otherwise "once" samples can still cut out synth drones.
	    // In a perfect world, culling for the purpose of "soliciting" a Voice would also count the new Voice being
	    // solicited, preferring to cut out that same Sound's old, say, one Voice, than another Sound's only Voice
	    + ((uint32_t)std::min<size_t>(num_active_voices, 7) << 27)

	    // Bits 24-26 - envelope state
	    + ((uint32_t)envelopes[0].stage() << 24)

	    // Bits  0-23 - time entered
	    + ((uint32_t)(-envelope0TimeEntered) & (0xFFFFFFFF >> 8));
}

uint32_t Voice::getPriorityRating() const {
	return priority_rating(sound.snapshot().voicePriority, sound.numActiveVoices());
}

void Voice::stampEnvelope0StageEntry(deluge::synth::VoiceClock clock) {
	if (envelopes[0].takeStageChanged()) {
		envelope0TimeEntered = clock.tick();
	}
}

void Voice::resetEnvelope0TimeEntered(deluge::synth::VoiceClock clock) {
	if (envelopes[0].stage() == Envelope::Stage::SUSTAIN) {
		envelope0TimeEntered = clock.tick();
	}
}
