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
#include "arm_neon_shim.h"
#include "definitions_cxx.hpp"
#include "dsp/dx/engine.h"
#include "dsp/filter/filter_set.h"
#include "dsp/oscillators/sine_osc.h"
#include "dsp/shaper_buffer.h"
#include "dsp/timestretch/time_stretcher.h"
#include "dsp/util.hpp"
#include "gui/waveform/waveform_renderer.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/sample/sample_cache.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/song/song.h"
#include "model/voice/voice_sample.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/live/live_pitch_shifter.h"
#include "processing/render_wave.h"
#include "processing/sound/sound.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/storage_manager.h"
#include "storage/wave_table/wave_table.h"

#include "dsp/oscillators/basic_waves.h"
#include "dsp/oscillators/oscillator.h"
#include "util/misc.h"
#include <cstring>
#include <new>

extern "C" {
#include "RZA1/mtu/mtu.h"
#include "drivers/ssi/ssi.h"
}

using namespace deluge;
namespace params = deluge::modulation::params;

PLACE_INTERNAL_FRUNK int32_t spareRenderingBuffer[4][SSI_TX_BUFFER_NUM_SAMPLES]
    __attribute__((aligned(CACHE_LINE_SIZE)));

// Hopefully I could make this use the spareRenderingBuffer instead...

const Patcher::Config kPatcherConfigForVoice = {
    .firstParam = 0,
    .firstNonVolumeParam = params::FIRST_LOCAL_NON_VOLUME,
    .firstHybridParam = params::FIRST_LOCAL__HYBRID,
    .firstZoneParam = params::FIRST_LOCAL_ZONE,
    .firstExpParam = params::FIRST_LOCAL_EXP,
    .endParams = params::FIRST_GLOBAL,
    .globality = GLOBALITY_LOCAL,
};

int32_t Voice::combineExpressionValues(const Sound& sound, int32_t expressionDimension) const {
	int32_t synthLevelValue = sound.monophonicExpressionValues[expressionDimension];
	int32_t voiceLevelValue = this->localExpressionSourceValuesBeforeSmoothing[expressionDimension];
	int32_t combinedValue = (synthLevelValue >> 1) + (voiceLevelValue >> 1);
	return lshiftAndSaturate<1>(combinedValue);
}

Voice::Voice(Sound& sound) : patcher(kPatcherConfigForVoice, sourceValues, paramFinalValues), sound{sound} {
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
		for (int32_t u = 0; u < this->sound.numUnison; u++) {
			unisonParts[u].sources[s].unassign(deletingSong);
		}
	}
}

uint32_t lastSoundOrder = 0;

// Parse BPM from sample filename. Handles both explicit "120BPM" and bare numbers
// like "drums_120_loop.wav" with sanity checking (range 40-300).
// Returns 0 if no valid BPM found.
static constexpr int32_t kMinPlausibleBPM = 40;
static constexpr int32_t kMaxPlausibleBPM = 300;

static int32_t extractNumber(const char* start, const char* end) {
	int32_t val = 0;
	for (const char* d = start; d < end; d++) {
		val = val * 10 + (*d - '0');
	}
	return val;
}

static int32_t parseBPMFromFilename(const char* path) {
	if (!path || !*path) {
		return 0;
	}

	// Find the filename portion (after last '/')
	const char* filename = path;
	for (const char* p = path; *p; p++) {
		if (*p == '/') {
			filename = p + 1;
		}
	}

	// First pass: look for explicit [digits]BPM pattern (highest confidence)
	for (const char* p = filename; *p; p++) {
		if ((p[0] == 'B' || p[0] == 'b') && (p[1] == 'P' || p[1] == 'p') && (p[2] == 'M' || p[2] == 'm')
		    && (p[3] == '.' || p[3] == '_' || p[3] == '-' || p[3] == ' ' || p[3] == '\0')) {
			const char* end = p;
			const char* start = p;
			while (start > filename && start[-1] >= '0' && start[-1] <= '9') {
				start--;
			}
			if (start < end) {
				int32_t bpm = extractNumber(start, end);
				if (bpm >= kMinPlausibleBPM && bpm <= kMaxPlausibleBPM) {
					return bpm;
				}
			}
		}
	}

	// Second pass: look for bare numbers delimited by separators (_, -, space, or start/end)
	// that fall in a plausible BPM range
	for (const char* p = filename; *p; p++) {
		if (*p >= '0' && *p <= '9') {
			// Check that this number is at a word boundary (start of filename or after separator)
			if (p > filename && p[-1] != '_' && p[-1] != '-' && p[-1] != ' ') {
				// Skip this digit sequence — it's embedded in a word
				while (*p >= '0' && *p <= '9') {
					p++;
				}
				if (!*p) {
					break;
				}
				continue;
			}
			const char* start = p;
			while (*p >= '0' && *p <= '9') {
				p++;
			}
			// Check trailing boundary (separator, dot/extension, or end)
			if (*p == '_' || *p == '-' || *p == ' ' || *p == '.' || *p == '\0') {
				int32_t val = extractNumber(start, p);
				if (val >= kMinPlausibleBPM && val <= kMaxPlausibleBPM) {
					return val;
				}
			}
			if (!*p) {
				break;
			}
		}
	}
	return 0;
}

// Returns false if fail and we need to unassign again
bool Voice::noteOn(ModelStackWithSoundFlags* modelStack, int32_t newNoteCodeBeforeArpeggiation,
                   int32_t newNoteCodeAfterArpeggiation, uint8_t velocity, uint32_t newSampleSyncLength,
                   int32_t ticksLate, uint32_t samplesLate, bool resetEnvelopes, int32_t newFromMIDIChannel,
                   const int16_t* mpeValues) {

	GeneralMemoryAllocator::get().checkStack("Voice::noteOn");

	inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] = newNoteCodeBeforeArpeggiation;
	inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)] = newFromMIDIChannel;
	noteCodeAfterArpeggiation = newNoteCodeAfterArpeggiation;
	orderSounded = lastSoundOrder++;
	overrideAmplitudeEnvelopeReleaseRate = 0;

	if (newNoteCodeAfterArpeggiation >= 128) {
		sourceValues[util::to_underlying(PatchSource::NOTE)] = 2147483647;
	}
	else if (newNoteCodeAfterArpeggiation <= 0) {
		sourceValues[util::to_underlying(PatchSource::NOTE)] = -2147483648;
	}
	else {
		sourceValues[util::to_underlying(PatchSource::NOTE)] = ((int32_t)newNoteCodeAfterArpeggiation - 64) * 33554432;
	}

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;
	Sound& sound = *static_cast<Sound*>(modelStack->modControllable);

	// Setup "half-baked" envelope output values. These need to exist before we do the initial patching below - and it's
	// only after that that we can render the "actual" envelope output values, taking their own input patching into
	// account.
	for (int32_t e = 0; e < kNumEnvelopes; e++) {

		// If no attack-stage...
		if (paramManager->getPatchedParamSet()->getValue(params::LOCAL_ENV_0_ATTACK + e) == -2147483648u) {
			envelopes[e].lastValue = 2147483647;
		}

		// Otherwise...
		else {
			envelopes[e].lastValue = 0;
		}
	}

	// Setup and render local LFO
	lfo2.setLocalInitialPhase(sound.lfoConfig[LFO2_ID]);
	sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_1)] = lfo2.render(0, sound.lfoConfig[LFO2_ID], 0);
	lfo4.setLocalInitialPhase(sound.lfoConfig[LFO4_ID]);
	sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_2)] = lfo4.render(0, sound.lfoConfig[LFO4_ID], 0);

	// Setup some sources which won't change for the duration of this note
	sourceValues[util::to_underlying(PatchSource::VELOCITY)] =
	    (velocity == 128) ? 2147483647 : ((int32_t)velocity - 64) * 33554432;

	// "Random" source
	sourceValues[util::to_underlying(PatchSource::RANDOM)] = getNoise();

	for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
		localExpressionSourceValuesBeforeSmoothing[m] = mpeValues[m] << 16;
		sourceValues[util::to_underlying(PatchSource::X) + m] = combineExpressionValues(sound, m);
	}

	if (resetEnvelopes) {
		sourceAmplitudesLastTime.fill(0);
		modulatorAmplitudeLastTime.fill(0);
		overallOscAmplitudeLastTime = 0;
		doneFirstRender = false;

		filterSet.reset();

		lastSaturationTanHWorkingValue[0] = 2147483648;
		lastSaturationTanHWorkingValue[1] = 2147483648;

		// Reset sine shaper state (DC blocker, feedback, stereo LFO)
		sineShaperState = dsp::SineShaperVoiceState{};
	}

	// Porta
	if (sound.polyphonic != PolyphonyMode::LEGATO
	    && paramManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_PORTAMENTO) != -2147483648
	    && sound.lastNoteCode != -2147483648) [[unlikely]] {
		setupPorta(sound);
	}

	else {
		portaEnvelopePos = 0xFFFFFFFF; // No porta
	}

	// Patch all sources to exclusive params, to give them an initial value. Exclusive params (params with just 1
	// source) aren't continuously recalculated, so they need that initial value. Remember, calculating that initial
	// value also takes into account the "preset value". This probably isn't strictly necessary for sources which we
	// know will be constantly changing, because that would make patching constantly calculate too. But that's only
	// really the envelopes, plus the LFOs (just the local one?) if they're not square
	for (int32_t s = 0; s < util::to_underlying(kFirstLocalSource); s++) {
		sourceValues[s] = sound.globalSourceValues[s];
	}
	patcher.performInitialPatching(sound, *paramManager);

	// Setup and render envelopes - again. Because they're local params (since mid-late 2017), we really need to render
	// them *after* initial patching is performed.
	for (int32_t e = 0; e < kNumEnvelopes; e++) {
		sourceValues[util::to_underlying(PatchSource::ENVELOPE_0) + e] = envelopes[e].noteOn(e, &sound, this);
	}

	if (resetEnvelopes) {
		for (int32_t s = 0; s < kNumSources; s++) {
			sourceWaveIndexesLastTime[s] = paramFinalValues[params::LOCAL_OSC_A_WAVE_INDEX + s];
		}
	}

	// Make all VoiceUnisonPartSources "active" by default
	for (int32_t s = 0; s < kNumSources; s++) {

		// Various stuff in this block is only relevant for OscType::SAMPLE, but no real harm in it just happening in
		// other cases.
		guides[s].audioFileHolder = nullptr;

		bool sourceEverActive = modelStack->checkSourceEverActive(s);
		if (sourceEverActive) [[likely]] {
			guides[s].noteOffReceived = false;
			guides[s].sequenceSyncLengthTicks = 0; // That's the default - may get overwritten below

			if (sound.getSynthMode() != SynthMode::FM
			    && (sound.sources[s].oscType == OscType::SAMPLE || sound.sources[s].oscType == OscType::WAVETABLE)) {

				// Set up MultiRange
				MultiRange* range = sound.sources[s].getRange(noteCodeAfterArpeggiation + sound.transpose);
				if (!range) { // There could be no Range for a SAMPLE or WAVETABLE Source that just hasn't had a file
					          // loaded, like how OSC2 very often would be sitting
					goto gotInactive;
				}

				AudioFileHolder* holder = range->getAudioFileHolder();
				// Only actually set the Range as ours if it has an AudioFile - so that we'll always know that any
				// VoiceSource's range definitely has a sample
				if (!holder || !holder->audioFile) {
					goto gotInactive;
				}

				guides[s].audioFileHolder = holder;

				if (sound.sources[s].oscType == OscType::SAMPLE) {

					if (sound.sources[s].repeatMode == SampleRepeatMode::STRETCH) {
						guides[s].sequenceSyncLengthTicks = newSampleSyncLength;
						guides[s].sequenceSyncStartedAtTick =
						    playbackHandler.lastSwungTickActioned
						    - ticksLate; // No harm setting this even if it's not valid or needed.
						                 // And yes, this is supposed to use lastSwungTickActioned, not
						                 // getActualSwungTickCount(). ticksLate is relative to that.
					}
					else if (sound.sources[s].repeatMode == SampleRepeatMode::PHASE_LOCKED) {
						// Try to parse BPM from the sample filename
						int32_t sampleBPM = parseBPMFromFilename(holder->filePath.get());
						if (sampleBPM > 0) {
							int64_t currentTick = playbackHandler.lastSwungTickActioned - ticksLate;

							// Compute the sample's musical length in ticks from its BPM.
							// syncLength = sampleBeats * ticksPerBeat
							//            = (sampleSamples / sampleRate / 60 * bpm) * quarterNoteLen
							uint32_t sampleLengthInSamples =
							    ((SampleHolder*)holder)->getLengthInSamplesAtSystemSampleRate(true);
							uint32_t quarterNoteLength = currentSong->getQuarterNoteLength();
							uint64_t syncLengthBig = (uint64_t)sampleLengthInSamples * sampleBPM * quarterNoteLength;
							uint32_t syncLength = syncLengthBig / ((uint64_t)kSampleRate * 60);

							if (syncLength > 0) {
								guides[s].sequenceSyncLengthTicks = syncLength;
								guides[s].sequenceSyncStartedAtTick = currentTick - (currentTick % syncLength);
							}
						}
						// No BPM in filename — play raw, no time-stretch or phase-lock
					}
				}
			}
		}
		else {
gotInactive:
			if (sound.getSynthMode() == SynthMode::RINGMOD) {
				return false;
			}
			sourceEverActive = false;
		}

activenessDetermined:
		for (int32_t u = 0; u < sound.numUnison; u++) {
			unisonParts[u].sources[s].active = sourceEverActive;
		}
	}

	calculatePhaseIncrements(modelStack);

	for (int32_t s = 0; s < kNumSources; s++) {

		bool sourceEverActive = modelStack->checkSourceEverActive(s);

		if (!sourceEverActive) [[unlikely]] {
			continue;
		}

		Source* source = &sound.sources[s];

		// FM overrides osc type to always be sines
		OscType oscType;
		if (sound.getSynthMode() == SynthMode::FM) {
			oscType = OscType::SINE;
		}
		else {
			oscType = source->oscType;
		}

		// int32_t samplesLateHere = samplesLate; // Make our own copy of this - we're going to deactivate it if we're
		// in STRETCH mode, cos that works differently

		if (oscType == OscType::SAMPLE && guides[s].audioFileHolder) {
			source->sampleControls.invertReversed = sound.invertReversed; // Copy the temporary flag from the sound
			guides[s].setupPlaybackBounds(source->sampleControls.isCurrentlyReversed());

			// Apply plocked sample start offset
			int32_t startOffsetParam =
			    paramManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_SAMPLE_START_OFFSET_A + s);
			guides[s].preRollSamples = 0;
			if (startOffsetParam != 0) {
				bool synced = (source->repeatMode == SampleRepeatMode::STRETCH
				               || source->repeatMode == SampleRepeatMode::PHASE_LOCKED)
				              && guides[s].sequenceSyncLengthTicks > 0;

				if (synced) {
					// For synced modes, apply offset as a tick shift so the phase-lock
					// seeking starts at the offset position. This only affects the initial
					// entry point; the loop continues in phase from there.
					int64_t tickShift = ((int64_t)startOffsetParam * (int64_t)guides[s].sequenceSyncLengthTicks) >> 31;
					guides[s].sequenceSyncStartedAtTick -= static_cast<int32_t>(tickShift);
				}
				else {
					// For non-synced modes, apply offset by modifying byte positions
					Sample* offsetSample = static_cast<Sample*>(guides[s].audioFileHolder->audioFile);
					int32_t bytesPerFrame = offsetSample->numChannels * offsetSample->byteDepth;
					int32_t regionBytes = guides[s].endPlaybackAtByte - guides[s].startPlaybackAtByte;
					int32_t absRegion = std::abs(regionBytes);
					bool forward = (regionBytes > 0);

					if (absRegion > 0 && bytesPerFrame > 0) {
						if (startOffsetParam > 0) {
							// Positive offset: shift start forward into the region
							int64_t offsetBytes = ((int64_t)startOffsetParam * absRegion) >> 31;
							int32_t fwd = static_cast<int32_t>(((offsetBytes % absRegion) + absRegion) % absRegion);
							fwd = (fwd / bytesPerFrame) * bytesPerFrame;
							guides[s].startPlaybackAtByte += forward ? fwd : -fwd;
						}
						else {
							// Negative offset: move start backward into audio before the
							// start marker. If we run out, wrap (LOOP) or silence (ONCE/CUT).
							// TODO: add "playoffset/slide" mode that shifts both start and end
							// points together, with optional crossfade using the voice envelope.
							int32_t desiredBackward =
							    static_cast<int32_t>((-(int64_t)startOffsetParam * absRegion) >> 31);
							desiredBackward = (desiredBackward / bytesPerFrame) * bytesPerFrame;

							int32_t availableBackward;
							if (forward) {
								availableBackward = guides[s].startPlaybackAtByte
								                    - static_cast<int32_t>(offsetSample->audioDataStartPosBytes);
							}
							else {
								int32_t audioEndByte = static_cast<int32_t>(offsetSample->audioDataStartPosBytes
								                                            + offsetSample->audioDataLengthBytes);
								availableBackward = audioEndByte - guides[s].startPlaybackAtByte;
							}
							if (availableBackward < 0) {
								availableBackward = 0;
							}
							availableBackward = (availableBackward / bytesPerFrame) * bytesPerFrame;

							int32_t actualBackward = std::min(desiredBackward, availableBackward);
							guides[s].startPlaybackAtByte += forward ? -actualBackward : actualBackward;

							int32_t remainingBytes = desiredBackward - actualBackward;
							if (remainingBytes > 0) {
								if (source->repeatMode == SampleRepeatMode::LOOP) {
									// Wrap remaining from end of region
									int32_t wrapped = remainingBytes % absRegion;
									wrapped = (wrapped / bytesPerFrame) * bytesPerFrame;
									if (forward) {
										guides[s].startPlaybackAtByte = guides[s].endPlaybackAtByte - wrapped;
									}
									else {
										guides[s].startPlaybackAtByte = guides[s].endPlaybackAtByte + wrapped;
									}
								}
								else {
									// ONCE/CUT: pad with silence pre-roll
									guides[s].preRollSamples = remainingBytes / bytesPerFrame;
								}
							}
						}
					}
				}
			}

			// if (source->repeatMode == SampleRepeatMode::STRETCH) samplesLateHere = 0;
		}

		// For PHASE_LOCKED, force late-start path to seek to bar-phase position
		// (but only when we're actually mid-bar, not on the downbeat)
		uint16_t effectiveSamplesLate = samplesLate;
		if (source->oscType == OscType::SAMPLE && source->repeatMode == SampleRepeatMode::PHASE_LOCKED
		    && guides[s].sequenceSyncLengthTicks) {
			int64_t tickIntoBar =
			    (playbackHandler.lastSwungTickActioned - ticksLate) - guides[s].sequenceSyncStartedAtTick;
			if (tickIntoBar > 0) {
				effectiveSamplesLate = std::max<uint16_t>(1, samplesLate);
			}
		}

		for (int32_t u = 0; u < sound.numUnison; u++) {

			// Check that we already marked this unison-part-source as active. Among other things, this ensures that if
			// the osc is set to SAMPLE, there actually is a sample loaded.
			if (unisonParts[u].sources[s].active) {
				bool success = unisonParts[u].sources[s].noteOn(this, source, &guides[s], effectiveSamplesLate,
				                                                sound.oscRetriggerPhase[s], resetEnvelopes,
				                                                sound.synthMode, velocity);
				if (!success) [[unlikely]] {
					return false; // This shouldn't really ever happen I don't think really...
				}
			}
		}
	}

	if (sound.getSynthMode() == SynthMode::FM) {
		uint32_t initialPhase = getOscInitialPhaseForZero(OscType::SINE);
		for (int32_t u = 0; u < sound.numUnison; u++) {
			for (int32_t m = 0; m < kNumModulators; m++) {
				if (sound.modulatorRetriggerPhase[m] != 0xFFFFFFFF) {
					unisonParts[u].modulatorPhase[m] = initialPhase + sound.modulatorRetriggerPhase[m];
				}
				if (resetEnvelopes) {
					unisonParts[u].modulatorFeedback[m] = 0;
				}
			}
		}
	}

	previouslyIgnoredNoteOff = false;
	expressionSourcesCurrentlySmoothing.reset();
	filterGainLastTime = 0;

	return true;
}

void Voice::expressionEventImmediate(const Sound& sound, int32_t voiceLevelValue, int32_t s) {
	int32_t expressionDimension = s - util::to_underlying(PatchSource::X);
	localExpressionSourceValuesBeforeSmoothing[expressionDimension] = voiceLevelValue;
	expressionSourcesFinalValueChanged[expressionDimension] = true;

	sourceValues[s] = combineExpressionValues(sound, expressionDimension);
}

void Voice::expressionEventSmooth(int32_t newValue, int32_t s) {
	int32_t expressionDimension = s - util::to_underlying(PatchSource::X);
	localExpressionSourceValuesBeforeSmoothing[expressionDimension] = newValue;
	expressionSourcesCurrentlySmoothing[expressionDimension] = true;
}

void Voice::changeNoteCode(ModelStackWithSoundFlags* modelStack, int32_t newNoteCodeBeforeArpeggiation,
                           int32_t newNoteCodeAfterArpeggiation, int32_t newInputMIDIChannel,
                           const int16_t* newMPEValues) {
	inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] = newNoteCodeBeforeArpeggiation;
	inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)] = newInputMIDIChannel;
	noteCodeAfterArpeggiation = newNoteCodeAfterArpeggiation;

	// We definitely want to go to these values smoothly. Probably wish it was even smoother... Actually nah this sounds
	// / feels great!
	for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
		localExpressionSourceValuesBeforeSmoothing[m] = newMPEValues[m] << 16;
		// TODO: what if there's just channel aftertouch, and it's still held down...
	}
	expressionSourcesCurrentlySmoothing = 0b111;

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;
	Sound& sound = *static_cast<Sound*>(modelStack->modControllable);

	if (paramManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_PORTAMENTO) != -2147483648) {
		setupPorta(sound);
	}

	calculatePhaseIncrements(modelStack);
}

void Voice::setupPorta(const Sound& sound) {
	portaEnvelopePos = 0;
	int32_t semitoneAdjustment = sound.lastNoteCode - noteCodeAfterArpeggiation;

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

void Voice::randomizeOscPhases(const Sound& sound) {
	for (int32_t u = 0; u < sound.numUnison; u++) {
		for (int32_t s = 0; s < kNumSources; s++) {
			unisonParts[u].sources[s].oscPos = getNoise();
			// TODO: we should do sample play pos, too
		}
		if (sound.getSynthMode() == SynthMode::FM) {
			for (int32_t m = 0; m < kNumModulators; m++) {
				unisonParts[u].modulatorPhase[m] = getNoise();
			}
		}
	}
}

// Can accept NULL paramManager
void Voice::calculatePhaseIncrements(ModelStackWithSoundFlags* modelStack) {

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;
	Sound& sound = *static_cast<Sound*>(modelStack->modControllable);

	int32_t noteCodeWithMasterTranspose = noteCodeAfterArpeggiation + sound.transpose;

	for (int32_t s = 0; s < kNumSources; s++) {

		if (!modelStack->checkSourceEverActive(s)) { // Sets all unison parts inactive by default

makeInactive: // Frequency too high to render! (Higher than 22.05kHz)
			for (int32_t u = 0; u < sound.numUnison; u++) {
				unisonParts[u].sources[s].active = false;
			}
			continue;
		}

		Source* source = &sound.sources[s];

		int32_t oscillatorTranspose;
		if (source->oscType == OscType::SAMPLE && guides[s].audioFileHolder) { // Do not do this for WaveTables
			oscillatorTranspose = ((SampleHolderForVoice*)guides[s].audioFileHolder)->transpose;
		}
		else {
			oscillatorTranspose = source->transpose;
		}

		int32_t transposedNoteCode = noteCodeWithMasterTranspose + oscillatorTranspose;

		uint32_t phaseIncrement;

		// Sample-osc
		if (sound.getSynthMode() != SynthMode::FM
		    && (source->oscType == OscType::SAMPLE || source->oscType == OscType::INPUT_L
		        || source->oscType == OscType::INPUT_R || source->oscType == OscType::INPUT_STEREO)) {

			int32_t pitchAdjustNeutralValue;
			if (source->oscType == OscType::SAMPLE) {
				pitchAdjustNeutralValue = ((SampleHolder*)guides[s].audioFileHolder)->neutralPhaseIncrement;
			}
			else {
				pitchAdjustNeutralValue = kMaxSampleValue;
			}

			int32_t noteWithinOctave = (uint16_t)(transposedNoteCode + 240) % 12;
			int32_t octave = (uint16_t)(transposedNoteCode + 120) / 12;

			phaseIncrement = multiply_32x32_rshift32(noteIntervalTable[noteWithinOctave], pitchAdjustNeutralValue);

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
		if (source->oscType == OscType::SAMPLE) { // guides[s].sampleHolder
			phaseIncrement = ((SampleHolderForVoice*)guides[s].audioFileHolder)->fineTuner.detune(phaseIncrement);
		}
		else {
			phaseIncrement = source->fineTuner.detune(phaseIncrement);
		}

		// If only one unison
		if (sound.numUnison == 1) {
			unisonParts[0].sources[s].phaseIncrementStoredValue = phaseIncrement;
		}

		// Or if multiple unison
		else {
			for (int32_t u = 0; u < sound.numUnison; u++) {
				unisonParts[u].sources[s].phaseIncrementStoredValue = sound.unisonDetuners[u].detune(phaseIncrement);
			}
		}
	}

	// FM modulators
	if (sound.getSynthMode() == SynthMode::FM) {
		for (int32_t m = 0; m < kNumModulators; m++) {

			if (sound.getSmoothedPatchedParamValue(params::LOCAL_MODULATOR_0_VOLUME + m, *paramManager)
			    == -2147483648) {
				continue; // Only if modulator active
			}

			int32_t transposedNoteCode = noteCodeWithMasterTranspose + sound.modulatorTranspose[m];
			int32_t noteWithinOctave = (transposedNoteCode + 120 - 4) % 12;
			int32_t octave = (transposedNoteCode + 120 - 4) / 12;
			int32_t shiftRightAmount = 20 - octave;

			int32_t phaseIncrement;

			if (shiftRightAmount >= 0) {
				phaseIncrement = noteFrequencyTable[noteWithinOctave] >> shiftRightAmount;
			}

			else {
				// Frequency too high to render! (Higher than 22.05kHz)
				for (int32_t u = 0; u < sound.numUnison; u++) {
					unisonParts[u].modulatorPhaseIncrement[m] = 0xFFFFFFFF; // Means "inactive"
				}
				continue;
			}

			// Cents
			phaseIncrement = sound.modulatorTransposers[m].detune(phaseIncrement);

			// If only one unison
			if (sound.numUnison == 1) {
				unisonParts[0].modulatorPhaseIncrement[m] = phaseIncrement;
			}

			// Or if multiple unison
			else {
				for (int32_t u = 0; u < sound.numUnison; u++) {
					unisonParts[u].modulatorPhaseIncrement[m] = sound.unisonDetuners[u].detune(phaseIncrement);
				}
			}
		}
	}
}

void Voice::noteOff(ModelStackWithSoundFlags* modelStack, bool allowReleaseStage) {

	for (int32_t s = 0; s < kNumSources; s++) {
		guides[s].noteOffReceived = true;
	}

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;
	Sound& sound = *static_cast<Sound*>(modelStack->modControllable);

	// Only do it if note-offs are meant to be processed for this sound. Otherwise ignore it.
	if (sound.allowNoteTails(modelStack, true)) {

		// If no release-stage, we'll stop as soon as we can
		if (!allowReleaseStage || !hasReleaseStage()) {
			envelopes[0].unconditionalRelease(EnvelopeStage::FAST_RELEASE);
		}

		// Or, do the release-stage
		else {
			envelopes[0].noteOff(0, &sound, paramManager);

			// Only start releasing envelope 2 if release wasn't at max value
			if (sound.paramFinalValues[params::LOCAL_ENV_1_RELEASE] >= 9) {
				envelopes[1].noteOff(1, &sound, paramManager);
			}
		}
	}

	else {
		previouslyIgnoredNoteOff = true;
	}

	if (sound.synthMode != SynthMode::FM) {
		for (int32_t s = 0; s < kNumSources; s++) {
			if (sound.sources[s].oscType == OscType::SAMPLE && guides[s].loopEndPlaybackAtByte) {
				for (int32_t u = 0; u < sound.numUnison; u++) {
					if (unisonParts[u].sources[s].active) {

						bool success =
						    unisonParts[u].sources[s].voiceSample->noteOffWhenLoopEndPointExists(this, &guides[s]);

						if (!success) {
							unisonParts[u].sources[s].unassign(false);
						}
					}
				}
			}
			else if (sound.sources[s].oscType == OscType::DX7) {
				for (int u = 0; u < sound.numUnison; u++) {
					if (unisonParts[u].sources[s].dxVoice) {
						unisonParts[u].sources[s].dxVoice->keyup();
					}
				}
			}
		}
	}

	for (int32_t s = 0; s < kNumSources; s++) {
		Source* source = &sound.sources[s];
		if (source->oscType == OscType::SAMPLE) {
			source->sampleControls.invertReversed = false; // Reset temporary flag back to normal
		}
	}
}

// Returns false if voice needs unassigning now
bool Voice::sampleZoneChanged(ModelStackWithSoundFlags* modelStack, int32_t s, MarkerType markerType) {

	AudioFileHolder* holder = guides[s].audioFileHolder;
	if (!holder) {
		return true; // If no holder, that means this Source/Sample is not currently playing, e.g. because its volume
		             // was set to 0.
	}

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;
	const Sound& sound = *static_cast<Sound*>(modelStack->modControllable);

	const Source& source = sound.sources[s];
	Sample* sample = (Sample*)holder->audioFile;

	guides[s].setupPlaybackBounds(source.sampleControls.isCurrentlyReversed());

	LoopType loopingType = guides[s].getLoopingType(sound.sources[s]);

	// Check we're still within bounds - for each unison part.
	// Well, that is, make sure we're not past the new end. Being before the start is ok, because we'll come back into
	// the still-remaining part soon enough.

	bool anyStillActive = false;

	for (int32_t u = 0; u < sound.numUnison; u++) {
		VoiceUnisonPartSource* voiceUnisonPartSource = &unisonParts[u].sources[s];

		if (voiceUnisonPartSource->active) {
			bool stillActive = voiceUnisonPartSource->voiceSample->sampleZoneChanged(
			    &guides[s], sample, source.sampleControls.isCurrentlyReversed(), markerType, loopingType,
			    getPriorityRating());
			if (!stillActive) {
				D_PRINTLN("returned false ---------");
				voiceUnisonPartSource->unassign(false);
			}
			else {
				anyStillActive = true;
			}
		}
	}

	// If none of this source still active, and no noise, see if the other source still has any...
	if (!anyStillActive
	    && !paramManager->getPatchedParamSet()->params[params::LOCAL_NOISE_VOLUME].containsSomething(-2147483648)) {

		s = 1 - s;

		if (!modelStack->checkSourceEverActive(s)) {
			return false;
		}
		else {
			for (int32_t u = 0; u < sound.numUnison; u++) {
				VoiceUnisonPartSource* voiceUnisonPartSource = &unisonParts[u].sources[s];
				if (voiceUnisonPartSource->active) {
					return true;
				}
			}
		}
	}

	return true;
}

uint32_t Voice::getLocalLFOPhaseIncrement(LFO_ID lfoId, deluge::modulation::params::Local param) {
	LFOConfig& config = sound.lfoConfig[lfoId];
	if (config.syncLevel == SYNC_LEVEL_NONE) {
		return paramFinalValues[param];
	}
	return sound.getSyncedLFOPhaseIncrement(config);
}

// Before calling this, you must set the filterSetConfig's doLPF and doHPF to default values

// Returns false if became inactive and needs unassigning
[[gnu::hot]] bool Voice::render(ModelStackWithSoundFlags* modelStack, int32_t* soundBuffer, int32_t numSamples,
                                bool soundRenderingInStereo, bool applyingPanAtVoiceLevel, uint32_t sourcesChanged,
                                bool doLPF, bool doHPF, int32_t externalPitchAdjust) {
	// we spread out over a render cycle - allocating and starting the voice takes more time than rendering it so this
	// avoids the cpu spike at note on
	if (justCreated == false) {
		justCreated = true;
		return true;
	}
	GeneralMemoryAllocator::get().checkStack("Voice::render");

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;
	Sound& sound = *static_cast<Sound*>(modelStack->modControllable);

	bool didStereoTempBuffer = false;

	// If we've previously ignored a note-off, we need to check that the user hasn't changed the preset so that we're
	// now waiting for a note-off again
	if (previouslyIgnoredNoteOff && sound.allowNoteTails(modelStack, true)) {
		noteOff(modelStack);
	}

	// Do envelopes - if they're patched to something (always do the first one though)
	for (int32_t e = 0; e < kNumEnvelopes; e++) {
		if (e == 0
		    || (paramManager->getPatchCableSet()->sourcesPatchedToAnything[GLOBALITY_LOCAL]
		        & (1 << (util::to_underlying(PatchSource::ENVELOPE_0) + e)))) {
			int32_t old = sourceValues[util::to_underlying(PatchSource::ENVELOPE_0) + e];
			int32_t release = paramFinalValues[params::LOCAL_ENV_0_RELEASE + e];
			if (e == 0 && overrideAmplitudeEnvelopeReleaseRate) {
				release = overrideAmplitudeEnvelopeReleaseRate;
			}
			sourceValues[util::to_underlying(PatchSource::ENVELOPE_0) + e] =
			    envelopes[e].render(numSamples, paramFinalValues[params::LOCAL_ENV_0_ATTACK + e],
			                        paramFinalValues[params::LOCAL_ENV_0_DECAY + e],
			                        paramFinalValues[params::LOCAL_ENV_0_SUSTAIN + e], release, decayTableSmall8);
			uint32_t anyChange = (old != sourceValues[util::to_underlying(PatchSource::ENVELOPE_0) + e]);
			sourcesChanged |= anyChange << (util::to_underlying(PatchSource::ENVELOPE_0) + e);
		}
	}

	bool unassignVoiceAfter =
	    (envelopes[0].state == EnvelopeStage::OFF)
	    || (envelopes[0].state > EnvelopeStage::DECAY
	        && sourceValues[util::to_underlying(PatchSource::ENVELOPE_0)] == std::numeric_limits<int32_t>::min());
	// Local LFO
	if (paramManager->getPatchCableSet()->sourcesPatchedToAnything[GLOBALITY_LOCAL]
	    & (1 << util::to_underlying(PatchSource::LFO_LOCAL_1))) {
		int32_t old = sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_1)];
		// TODO: Same as with LFO1, there should be no reason to recompute the phase increment
		// for every sample.
		sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_1)] = lfo2.render(
		    numSamples, sound.lfoConfig[LFO2_ID], getLocalLFOPhaseIncrement(LFO2_ID, params::LOCAL_LFO_LOCAL_FREQ_1));
		uint32_t anyChange = (old != sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_1)]);
		sourcesChanged |= anyChange << util::to_underlying(PatchSource::LFO_LOCAL_1);
	}
	if (paramManager->getPatchCableSet()->sourcesPatchedToAnything[GLOBALITY_LOCAL]
	    & (1 << util::to_underlying(PatchSource::LFO_LOCAL_2))) {
		int32_t old = sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_2)];
		// TODO: Same as with LFO1, there should be no reason to recompute the phase increment
		// for every sample.
		sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_2)] = lfo4.render(
		    numSamples, sound.lfoConfig[LFO4_ID], getLocalLFOPhaseIncrement(LFO4_ID, params::LOCAL_LFO_LOCAL_FREQ_2));
		uint32_t anyChange = (old != sourceValues[util::to_underlying(PatchSource::LFO_LOCAL_2)]);
		sourcesChanged |= anyChange << util::to_underlying(PatchSource::LFO_LOCAL_2);
	}

	// MPE params

	expressionSourcesCurrentlySmoothing |= sound.expressionSourcesChangedAtSynthLevel;

	if (expressionSourcesCurrentlySmoothing.any()) {
		expressionSourcesFinalValueChanged |= expressionSourcesCurrentlySmoothing;

		for (int32_t i = 0; i < kNumExpressionDimensions; i++) {
			if (expressionSourcesCurrentlySmoothing[i]) {

				int32_t targetValue = combineExpressionValues(sound, i);

				int32_t diff = (targetValue >> 8) - (sourceValues[i + util::to_underlying(PatchSource::X)] >> 8);

				if (diff == 0) {
					expressionSourcesCurrentlySmoothing[i] = false;
				}
				else {
					int32_t amountToAdd = diff * numSamples;
					sourceValues[i + util::to_underlying(PatchSource::X)] += amountToAdd;
				}
			}
		}
	}

	sourcesChanged |= expressionSourcesFinalValueChanged.to_ulong() << util::to_underlying(PatchSource::X);

	expressionSourcesFinalValueChanged.reset();

	// Patch all the sources to their parameters
	if (sourcesChanged) {
		for (int32_t s = 0; s < util::to_underlying(kFirstLocalSource); s++) {
			sourceValues[s] = sound.globalSourceValues[s];
		}
		patcher.performPatching(sourcesChanged, sound, *paramManager);
	}

	// Sort out pitch
	int32_t overallPitchAdjust = paramFinalValues[params::LOCAL_PITCH_ADJUST];

	// Pitch adjust from "external" - e.g. the Kit
	if (externalPitchAdjust != kMaxSampleValue) {
		int32_t output = multiply_32x32_rshift32_rounded(overallPitchAdjust, externalPitchAdjust);
		if (output > 8388607) {
			output = 8388607; // Limit it a bit. Not really quite sure if necessary?
		}
		overallPitchAdjust = output << 8;
	}

	// Pitch adjust via MIDI pitch bend and MPE
	uint8_t const* bendRanges = FlashStorage::defaultBendRange;
	ExpressionParamSet* expressionParams = paramManager->getExpressionParamSet();
	if (expressionParams) {
		bendRanges = expressionParams->bendRanges;
	}

	int32_t totalBendAmount = // Comes out as a 32-bit number representing a range of +-192 semitones
	    (sound.monophonicExpressionValues[0] / 192) * bendRanges[BEND_RANGE_MAIN]
	    + (localExpressionSourceValuesBeforeSmoothing[0] / 192) * bendRanges[BEND_RANGE_FINGER_LEVEL];

	overallPitchAdjust = getExp(overallPitchAdjust, totalBendAmount >> 1);

	// Porta
	if (portaEnvelopePos < 8388608) {
		int32_t envValue = getDecay4(portaEnvelopePos, 23);
		int32_t pitchAdjustmentHere =
		    kMaxSampleValue + (multiply_32x32_rshift32_rounded(envValue, portaEnvelopeMaxAmplitude) << 1);

		int32_t a = multiply_32x32_rshift32_rounded(overallPitchAdjust, pitchAdjustmentHere);
		if (a > 8388607) {
			a = 8388607; // Prevent overflow! Happened to Matt Bates
		}
		overallPitchAdjust = a << 8;

		// Move envelope on. Using the "release rate" lookup table gives by far the best range of speed values
		int32_t envelopeSpeed = lookupReleaseRate(cableToExpParamShortcut(
		                            paramManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_PORTAMENTO)))
		                        >> 13;
		portaEnvelopePos += envelopeSpeed * numSamples;
	}

	// Decide whether to do an auto-release for sample. Despite this being envelope-related, meaning we'd ideally prefer
	// to do it before patching, we can only do it after because we need to know pitch

	// If not already releasing and some release is set, and no noise-source...
	//@todo - cache this, it never changes
	if (sound.getSynthMode() != SynthMode::FM && envelopes[0].state < EnvelopeStage::RELEASE && hasReleaseStage()
	    && !paramManager->getPatchedParamSet()->params[params::LOCAL_NOISE_VOLUME].containsSomething(-2147483648)) {

		uint32_t whichSourcesNeedAttention = 0;

		// We only want to do this if all active sources are play-once samples
		// For each source...
		for (int32_t s = 0; s < kNumSources; s++) {
			Source* source = &sound.sources[s];

			// If this source isn't enabled, skip it
			if (!modelStack->checkSourceEverActive(s)) {
				continue;
			}

			// If it's not a sample, or it's not a play-once, or it has a loop-end point but we haven't received the
			// note-off, then we don't want the auto-release feature for it
			if (source->oscType != OscType::SAMPLE
			    || source->repeatMode
			           != SampleRepeatMode::ONCE // Don't do it for anything else. STRETCH is too hard to calculate
			    || !guides[s].audioFileHolder
			    || (((SampleHolderForVoice*)guides[s].audioFileHolder)->loopEndPos && !guides[s].noteOffReceived)) {
				goto skipAutoRelease;
			}

			whichSourcesNeedAttention |= (1 << s);
		}

		// If either / both sources need attention...
		// only happens for auto release of play once samples - we probably don't get here
		if (whichSourcesNeedAttention) [[unlikely]] {

			int32_t releaseStageLengthSamples =
			    (uint32_t)8388608 / (uint32_t)paramFinalValues[params::LOCAL_ENV_0_RELEASE];

			int32_t highestNumSamplesLeft = 0;

			// For each source...
			for (int32_t s = 0; s < kNumSources; s++) {

				// If it needed attention...
				if (whichSourcesNeedAttention & (1 << s)) {

					// This Source needs an auto release applied. Calculate for the last unison, because that'll have
					// the higher pitch, so will be ending soonest
					VoiceUnisonPartSource* voiceUnisonPartSource = &unisonParts[sound.numUnison - 1].sources[s];
					if (!voiceUnisonPartSource->active) {
						continue;
					}

					VoiceSample* voiceSample = voiceUnisonPartSource->voiceSample;

					Sample* sample = (Sample*)guides[s].audioFileHolder->audioFile;
					Cluster* cluster = voiceSample->clusters[0];
					int32_t bytePos = voiceSample->getPlayByteLowLevel(sample, &guides[s]);

					int32_t bytesLeft =
					    (int32_t)((uint32_t)guides[s].endPlaybackAtByte - (uint32_t)bytePos) * guides[s].playDirection;

					Source* source = &sound.sources[s];
					int32_t bytesPerSample = sample->byteDepth * sample->numChannels;

					int32_t releaseStageLengthBytes = releaseStageLengthSamples * bytesPerSample;

					// Work out the actual sample read rate, from the "native" read rate for the last unison, combined
					// with the "pitch adjust" amount, and the pitch adjust for this source alone. If the pitch goes
					// crazy-high, this will fall through and prevent auto-release from happening
					uint32_t actualSampleReadRate = voiceUnisonPartSource->phaseIncrementStoredValue;
					if (!adjustPitch(actualSampleReadRate, overallPitchAdjust)) {
						continue;
					}
					if (!adjustPitch(actualSampleReadRate, paramFinalValues[params::LOCAL_OSC_A_PITCH_ADJUST + s])) {
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
			if (envelopes[0].state == EnvelopeStage::ATTACK && envelopes[0].pos == 0) {
				envelopes[0].lastValue = 2147483647;
			}
			envelopes[0].unconditionalRelease();
		}
	}
skipAutoRelease: {}

	if (!doneFirstRender && paramFinalValues[params::LOCAL_ENV_0_ATTACK] > 245632) {
		for (int32_t m = 0; m < kNumModulators; m++) {
			modulatorAmplitudeLastTime[m] = paramFinalValues[params::LOCAL_MODULATOR_0_VOLUME + m];
		}
	}

	// Apply envelope 0 to volume. This takes effect as a cut only; when the envelope is at max height, volume is
	// unaffected. Important that we use lshiftAndSaturate here - otherwise, number can overflow if combining high
	// velocity patching with big LFO
	int32_t overallOscAmplitude = lshiftAndSaturate<2>(
	    multiply_32x32_rshift32(paramFinalValues[params::LOCAL_VOLUME],
	                            (sourceValues[util::to_underlying(PatchSource::ENVELOPE_0)] >> 1) + 1073741824));

	// This is the gain which gets applied to compensate for any change in gain that the filter is going to cause
	int32_t filterGain;

	// Prepare the filters
	// Checking if filters should run now happens within the filterset
	FilterMode lpfMode = doLPF ? sound.lpfMode : FilterMode::OFF;
	FilterMode hpfMode = doHPF ? sound.hpfMode : FilterMode::OFF;
	filterGain = filterSet.setConfig(
	    paramFinalValues[params::LOCAL_LPF_FREQ], paramFinalValues[params::LOCAL_LPF_RESONANCE], lpfMode,
	    paramFinalValues[params::LOCAL_LPF_MORPH], paramFinalValues[params::LOCAL_HPF_FREQ],
	    (paramFinalValues[params::LOCAL_HPF_RESONANCE]), // >> StorageManager::devVarA) << StorageManager::devVarA,
	    hpfMode, paramFinalValues[params::LOCAL_HPF_MORPH], sound.volumeNeutralValueForUnison << 1, sound.filterRoute,
	    false, nullptr); // Level adjustment for unison now happens *before* the filter!

	SynthMode synthMode = sound.getSynthMode();

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
			overallOscAmplitude =
			    multiply_32x32_rshift32_rounded(overallOscAmplitude, sound.volumeNeutralValueForUnison) << 3;

			int32_t a = multiply_32x32_rshift32(paramFinalValues[params::LOCAL_OSC_A_VOLUME], overallOscAmplitude);
			int32_t b = multiply_32x32_rshift32(paramFinalValues[params::LOCAL_OSC_B_VOLUME], overallOscAmplitude);

			// Clip off those amplitudes before they get too high. I think these were originally intended to stop the
			// amplitude rising to more than "4", whatever that meant?
			sourceAmplitudes[0] = std::min(a, (int32_t)134217727);
			sourceAmplitudes[1] = std::min(b, (int32_t)134217727);
		}

		// Or if subtractive, we don't do that, because we do want to apply the overall amplitude *after* the filter
		else {
			if (sound.hasFilters()) {
				sourceAmplitudes[0] =
				    multiply_32x32_rshift32_rounded(paramFinalValues[params::LOCAL_OSC_A_VOLUME], filterGain);
				sourceAmplitudes[1] =
				    multiply_32x32_rshift32_rounded(paramFinalValues[params::LOCAL_OSC_B_VOLUME], filterGain);
			}
			else {
				sourceAmplitudes[0] = paramFinalValues[params::LOCAL_OSC_A_VOLUME] >> 4;
				sourceAmplitudes[1] = paramFinalValues[params::LOCAL_OSC_B_VOLUME] >> 4;
			}
		}

		bool shouldAvoidIncrementing = doneFirstRender ? (filterGainLastTime != filterGain)
		                                               : (paramFinalValues[params::LOCAL_ENV_0_ATTACK] > 245632);

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
				modulatorsActive[m] =
				    (paramFinalValues[params::LOCAL_MODULATOR_0_VOLUME + m] != 0 || modulatorAmplitudeLastTime[m] != 0);

				if (modulatorsActive[m]) {
					modulatorAmplitudeIncrements[m] = (int32_t)(paramFinalValues[params::LOCAL_MODULATOR_0_VOLUME + m]
					                                            - modulatorAmplitudeLastTime[m])
					                                  / numSamples;
				}
			}
		}
	}

	int32_t sourceWaveIndexIncrements[kNumSources];

	if (synthMode != SynthMode::FM) {
		if (!doneFirstRender && paramFinalValues[params::LOCAL_ENV_0_ATTACK] > 245632) {
			overallOscAmplitudeLastTime = overallOscAmplitude;
		}
		overallOscillatorAmplitudeIncrement = (int32_t)(overallOscAmplitude - overallOscAmplitudeLastTime) / numSamples;

		for (int32_t s = 0; s < kNumSources; s++) {
			sourceWaveIndexIncrements[s] =
			    (int32_t)(paramFinalValues[params::LOCAL_OSC_A_WAVE_INDEX + s] - sourceWaveIndexesLastTime[s])
			    / numSamples;
		}
	}

	doneFirstRender = true;

	uint32_t oscSyncPos[kMaxNumVoicesUnison];
	bool doingOscSync = sound.renderingOscillatorSyncCurrently(paramManager);

	// Oscillator sync
	if (doingOscSync) {
		for (int32_t u = 0; u < sound.numUnison; u++) {
			oscSyncPos[u] = unisonParts[u].sources[0].oscPos;
		}
	}

	// whether stereo unison actually is active. if stereo is being vetoed from higher up, don't do it.
	bool stereoUnison = sound.unisonStereoSpread && sound.numUnison > 1 && soundRenderingInStereo;

	int32_t* oscBuffer;
	bool anythingInOscBuffer = false;
	int32_t sourceAmplitudesNow[kNumSources];
	for (int32_t s = 0; s < kNumSources; s++) {
		sourceAmplitudesNow[s] = sourceAmplitudesLastTime[s];
	}

	int32_t amplitudeL, amplitudeR;
	bool doPanning;

	// two first indicies are reserved in case we need stereo for unison spread
	oscBuffer = spareRenderingBuffer[0];
	int32_t channels = stereoUnison ? 2 : 1;

	int32_t const* const oscBufferEnd = oscBuffer + numSamples * channels;

	// If any noise, do that. By cutting a corner here, we do it just once for all "unison", rather than for each
	// unison. Increasing number of unison cuts the volume of the oscillators
	if (paramFinalValues[params::LOCAL_NOISE_VOLUME] != 0 && synthMode != SynthMode::FM) [[unlikely]] {

		// This was >>2, but because I had a bug in V2.0.x which made noise too loud if filter on,
		// I'm now making this louder to compensate and remain consistent by going just >>1.
		// So now I really need to make it so that sounds made before V2.0 halve their noise volume... (Hey, did I
		// ever do this? Who knows...)
		int32_t n = paramFinalValues[params::LOCAL_NOISE_VOLUME] >> 1;
		if (sound.hasFilters()) {
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
		doPanning = (AudioEngine::renderInStereo
		             && shouldDoPanning(paramFinalValues[params::LOCAL_PAN], &amplitudeL, &amplitudeR));
	}

	uint32_t sourcesToRenderInStereo = 0;

	// Normal mode: subtractive / samples. We do each source first, for all unison
	if (synthMode == SynthMode::SUBTRACTIVE) {
		bool unisonPartBecameInactive = false;

		uint32_t oscSyncPhaseIncrement[kMaxNumVoicesUnison];

		// First, render any mono sources, and note whether there are any stereo ones
		for (int32_t s = 0; s < kNumSources; s++) {

			uint32_t* getPhaseIncrements = nullptr;
			bool getOutAfterGettingPhaseIncrements = false;

			// If we're doing osc sync and this is osc A...
			if ((s == 0) && doingOscSync) {
				getPhaseIncrements = oscSyncPhaseIncrement;
			}

			// This this source isn't active...
			if (!sound.isSourceActiveCurrently(s, paramManager)) {

				// If we're doing osc sync...
				if (getPhaseIncrements) {
					getOutAfterGettingPhaseIncrements = true;

					// Otherwise, skip it
				}
				else {
					continue;
				}
			}

			if (!sound.sources[s].renderInStereo(&sound, (SampleHolder*)guides[s].audioFileHolder)) {
				renderBasicSource(sound, paramManager, s, oscBuffer, numSamples, false, sourceAmplitudesNow[s],
				                  &unisonPartBecameInactive, overallPitchAdjust, (s == 1) && doingOscSync, oscSyncPos,
				                  oscSyncPhaseIncrement, sourceAmplitudeIncrements[s], getPhaseIncrements,
				                  getOutAfterGettingPhaseIncrements, sourceWaveIndexIncrements[s]);
				anythingInOscBuffer = true;
			}
			else {
				sourcesToRenderInStereo |= (1 << s);
			}
		}

		// If any sources need rendering in stereo
		if (sourcesToRenderInStereo) {

			// If we've already got something mono in the buffer, copy that to the right-channel buffer
			if (anythingInOscBuffer) {
				for (int32_t i = numSamples - 1; i >= 0; i--) {
					oscBuffer[(i << 1) + 1] = oscBuffer[i];
					oscBuffer[(i << 1)] = oscBuffer[i];
				}
			}

			// Otherwise, make it blank
			else {
				memset(&oscBuffer[numSamples], 0, numSamples * sizeof(int32_t));
			}

			// Render each source that's stereo
			for (int32_t s = 0; s < kNumSources; s++) {
				if (sourcesToRenderInStereo & (1 << s)) {
					renderBasicSource(sound, paramManager, s, oscBuffer, numSamples, true, sourceAmplitudesNow[s],
					                  &unisonPartBecameInactive, overallPitchAdjust, false, nullptr, nullptr,
					                  sourceAmplitudeIncrements[s], nullptr, false, sourceWaveIndexIncrements[s]);
				}
			}

			didStereoTempBuffer = true;
		}

		// If any unison part became inactive (for either source), and no noise-source, then it might be time to
		// unassign the voice...
		if (unisonPartBecameInactive && areAllUnisonPartsInactive(*modelStack)) {

			// If no filters, we can just unassign
			if (!filterSet.isOn()) {
				unassignVoiceAfter = true;
			}

			// Otherwise, must do a fast-release to avoid a click
			else {
				if (envelopes[0].state < EnvelopeStage::FAST_RELEASE) {
					envelopes[0].unconditionalRelease(EnvelopeStage::FAST_RELEASE);
				}
			}
		}
	}

	// Otherwise (FM and ringmod) we go through each unison first, and for each one we render both sources together
	else {
		if (stereoUnison) {
			// oscBuffer is always a stereo temp buffer
			didStereoTempBuffer = true;
		}

		// For each unison part
		for (int32_t u = 0; u < sound.numUnison; u++) {

			int32_t unisonAmplitudeL, unisonAmplitudeR;
			shouldDoPanning((stereoUnison ? sound.unisonPan[u] : 0), &unisonAmplitudeL, &unisonAmplitudeR);

			// Work out the phase increments of the two sources. If these are too high, sourceAmplitudes[s] is set to 0.
			// Yes this will affect all unison parts, which seems like it's not what we want, but since we're traversing
			// the unison parts in ascending frequency, it's fine!

			uint32_t phaseIncrements[kNumSources];
			for (int32_t s = 0; s < kNumSources; s++) {
				phaseIncrements[s] = unisonParts[u].sources[s].phaseIncrementStoredValue;
			}

			// If overall pitch adjusted...
			if (overallPitchAdjust != kMaxSampleValue) {
				for (int32_t s = 0; s < kNumSources; s++) {
					if (!adjustPitch(phaseIncrements[s], overallPitchAdjust)) {
						if (synthMode == SynthMode::RINGMOD) {
							goto skipUnisonPart;
						}
						else {
							sourceAmplitudes[s] = 0; // For FM
						}
					}
				}
			}

			// If individual source pitch adjusted...
			for (int32_t s = 0; s < kNumSources; s++) {
				if (!adjustPitch(phaseIncrements[s], paramFinalValues[params::LOCAL_OSC_A_PITCH_ADJUST + s])) {
					if (synthMode == SynthMode::RINGMOD) {
						goto skipUnisonPart;
					}
					else {
						sourceAmplitudes[s] = 0; // For FM
					}
				}
			}

			// If ringmod
			if (synthMode == SynthMode::RINGMOD) [[unlikely]] {

				int32_t amplitudeForRingMod = 1 << 27;

				if (sound.hasFilters()) {
					amplitudeForRingMod = multiply_32x32_rshift32_rounded(amplitudeForRingMod, filterGain) << 4;
				}

				bool doingOscSyncThisOscillator = false;
				int32_t s = 0;
				goto cantBeDoingOscSyncForFirstOsc;

				for (; s < 2; s++) {
					doingOscSyncThisOscillator = doingOscSync;

cantBeDoingOscSyncForFirstOsc:
					// Work out pulse width, from parameter. This has no effect if we're not actually using square
					// waves, but just do it anyway, it's a simple calculation
					int32_t pulseWidth =
					    (uint32_t)lshiftAndSaturate<1>(paramFinalValues[params::LOCAL_OSC_A_PHASE_WIDTH + s]);

					OscType oscType = sound.sources[s].oscType;

					dsp::Oscillator::renderOsc(
					    oscType, 0, spareRenderingBuffer[s + 2], spareRenderingBuffer[s + 2] + numSamples, numSamples,
					    phaseIncrements[s], pulseWidth, &unisonParts[u].sources[s].oscPos, false, 0,
					    doingOscSyncThisOscillator, oscSyncPos[u], phaseIncrements[0], sound.oscRetriggerPhase[s],
					    sourceWaveIndexIncrements[s], sourceWaveIndexesLastTime[s],
					    static_cast<WaveTable*>(guides[s].audioFileHolder->audioFile));

					// Sine and triangle waves come out bigger in fixed-amplitude rendering (for arbitrary reasons), so
					// we need to compensate
					if (oscType == OscType::SAW || oscType == OscType::ANALOG_SAW_2) {
						amplitudeForRingMod <<= 1;
					}
					else if (oscType == OscType::WAVETABLE) {
						amplitudeForRingMod <<= 2;
					}
				}

				int32_t* __restrict__ output = oscBuffer;
				int32_t* __restrict__ input0 = spareRenderingBuffer[2];
				int32_t* __restrict__ input1 = spareRenderingBuffer[3];

				if (stereoUnison) {
					int32_t const* const oscBufferEnd = oscBuffer + 2 * numSamples;
					do {
						int32_t out = multiply_32x32_rshift32_rounded(multiply_32x32_rshift32(*input0, *input1),
						                                              amplitudeForRingMod);
						*output++ += multiply_32x32_rshift32(out, unisonAmplitudeL) << 2;
						*output++ += multiply_32x32_rshift32(out, unisonAmplitudeR) << 2;
						input0++;
						input1++;
					} while (output != oscBufferEnd);
				}
				else {
					int32_t const* const oscBufferEnd = oscBuffer + numSamples;
					do {
						*output += multiply_32x32_rshift32_rounded(multiply_32x32_rshift32(*input0, *input1),
						                                           amplitudeForRingMod);
						input0++;
						input1++;
					} while (++output != oscBufferEnd);
				}
			}

			// Or if FM
			else {

				// If overall pitch adjusted, adjust modulator pitches
				uint32_t phaseIncrementModulator[kNumModulators];
				for (int32_t m = 0; m < kNumModulators; m++) {
					phaseIncrementModulator[m] = unisonParts[u].modulatorPhaseIncrement[m];
					if (phaseIncrementModulator[m] == 0xFFFFFFFF) {
						modulatorsActive[m] = false; // If frequency marked as too high
					}
				}

				if (overallPitchAdjust != kMaxSampleValue) {
					for (int32_t m = 0; m < kNumModulators; m++) {
						if (modulatorsActive[m]) {
							if (!adjustPitch(phaseIncrementModulator[m], overallPitchAdjust)) {
								modulatorsActive[m] = false;
							}
						}
					}
				}

				// Check if individual modulator pitches adjusted
				for (int32_t m = 0; m < kNumModulators; m++) {
					if (modulatorsActive[m]) {
						if (!adjustPitch(phaseIncrementModulator[m],
						                 paramFinalValues[params::LOCAL_MODULATOR_0_PITCH_ADJUST + m])) {
							modulatorsActive[m] = false;
						}
					}
				}

				int32_t* fmOscBuffer = oscBuffer;
				if (stereoUnison) {
					// buffer 0-1: stereo output, 2: modulators, 3: per-unison carriers
					fmOscBuffer = spareRenderingBuffer[3];
					memset(fmOscBuffer, 0, numSamples * sizeof(int32_t));
				}

				// Modulators
				if (modulatorsActive[1]) {

					// If special case where mod1 is modulating mod0 but mod0 is inactive, go away...
					if (sound.modulator1ToModulator0 && !modulatorsActive[0]) {
						goto noModulatorsActive;
					}

					// Render mod1
					renderSineWaveWithFeedback(spareRenderingBuffer[2], numSamples, &unisonParts[u].modulatorPhase[1],
					                           modulatorAmplitudeLastTime[1], phaseIncrementModulator[1],
					                           paramFinalValues[params::LOCAL_MODULATOR_1_FEEDBACK],
					                           &unisonParts[u].modulatorFeedback[1], false,
					                           modulatorAmplitudeIncrements[1]);

					// If mod1 is modulating mod0...
					if (sound.modulator1ToModulator0) {
						// .. render modulator0, receiving the FM from mod1
						renderFMWithFeedback(spareRenderingBuffer[2], numSamples, nullptr,
						                     &unisonParts[u].modulatorPhase[0], modulatorAmplitudeLastTime[0],
						                     phaseIncrementModulator[0],
						                     paramFinalValues[params::LOCAL_MODULATOR_0_FEEDBACK],
						                     &unisonParts[u].modulatorFeedback[0], modulatorAmplitudeIncrements[0]);
					}

					// Otherwise, so long as modulator0 is in fact active, render it separately and add it
					else if (modulatorsActive[0]) {
						renderSineWaveWithFeedback(
						    spareRenderingBuffer[2], numSamples, &unisonParts[u].modulatorPhase[0],
						    modulatorAmplitudeLastTime[0], phaseIncrementModulator[0],
						    paramFinalValues[params::LOCAL_MODULATOR_0_FEEDBACK], &unisonParts[u].modulatorFeedback[0],
						    true, modulatorAmplitudeIncrements[0]);
					}
				}
				else {
					if (modulatorsActive[0]) {
						renderSineWaveWithFeedback(
						    spareRenderingBuffer[2], numSamples, &unisonParts[u].modulatorPhase[0],
						    modulatorAmplitudeLastTime[0], phaseIncrementModulator[0],
						    paramFinalValues[params::LOCAL_MODULATOR_0_FEEDBACK], &unisonParts[u].modulatorFeedback[0],
						    false, modulatorAmplitudeIncrements[0]);
					}
					else {
noModulatorsActive:
						for (int32_t s = 0; s < kNumSources; s++) {
							if (sourceAmplitudes[s]) {
								renderSineWaveWithFeedback(
								    fmOscBuffer, numSamples, &unisonParts[u].sources[s].oscPos, sourceAmplitudesNow[s],
								    phaseIncrements[s], paramFinalValues[params::LOCAL_CARRIER_0_FEEDBACK + s],
								    &unisonParts[u].sources[s].carrierFeedback, true, sourceAmplitudeIncrements[s]);
							}
						}

						goto carriersDone;
					}
				}

				// Carriers
				for (int32_t s = 0; s < kNumSources; s++) {
					if (sourceAmplitudes[s]) {
						renderFMWithFeedbackAdd(
						    fmOscBuffer, numSamples, spareRenderingBuffer[2], &unisonParts[u].sources[s].oscPos,
						    sourceAmplitudesNow[s], phaseIncrements[s],
						    paramFinalValues[params::LOCAL_CARRIER_0_FEEDBACK + s],
						    &unisonParts[u].sources[s].carrierFeedback, sourceAmplitudeIncrements[s]);
					}
				}

carriersDone: {}
				if (stereoUnison) {
					// double up the temp buffer
					for (int32_t i = 0; i < numSamples; i++) {
						oscBuffer[(i << 1)] += multiply_32x32_rshift32(fmOscBuffer[i], unisonAmplitudeL) << 2;
						oscBuffer[(i << 1) + 1] += multiply_32x32_rshift32(fmOscBuffer[i], unisonAmplitudeR) << 2;
					}
				}
			}
		}

skipUnisonPart: {}
	}

	if (didStereoTempBuffer) {
		int32_t* const oscBufferEnd = oscBuffer + (numSamples << 1);
		// fold
		if (paramFinalValues[params::LOCAL_FOLD] > 0) {
			dsp::foldBufferPolyApproximation(oscBuffer, oscBufferEnd, paramFinalValues[params::LOCAL_FOLD]);
		}

		// Sine Shaper (per-voice, mod-matrix routable drive, harmonic, and twist)
		if (sound.sineShaper.isEnabled()) {
			std::span<StereoSample> stereoBuffer(reinterpret_cast<StereoSample*>(oscBuffer), numSamples);
			dsp::processSineShaper(stereoBuffer, &sound.sineShaper, &sineShaperState,
			                       paramFinalValues[params::LOCAL_SINE_SHAPER_DRIVE],
			                       paramManager->getPatchedParamSet()->getValue(params::LOCAL_SINE_SHAPER_HARMONIC),
			                       paramFinalValues[params::LOCAL_SINE_SHAPER_HARMONIC],
			                       paramManager->getPatchedParamSet()->getValue(params::LOCAL_SINE_SHAPER_TWIST),
			                       paramFinalValues[params::LOCAL_SINE_SHAPER_TWIST], 0, // filterGain: 0 = FM mode
			                       sound.hasFilters());
		}

		// Table Shaper (per-voice, mod-matrix routable drive and mix)
		if (sound.shaper.shapeX > 0) {
			q31_t satDrive = paramFinalValues[params::LOCAL_TABLE_SHAPER_DRIVE];
			q31_t satMix = paramFinalValues[params::LOCAL_TABLE_SHAPER_MIX];
			// Convert buffer to StereoSample span
			std::span<StereoSample> stereoBuffer(reinterpret_cast<StereoSample*>(oscBuffer), numSamples);
			// Compute note frequency for LPF cutoff scaling
			float noteFreqHz = 440.0f * powf(2.0f, (noteCodeAfterArpeggiation - 69) / 12.0f);
			dsp::shapeBufferInt32(stereoBuffer, sound.shaperDsp, satDrive, &sound.shaper.driveLast, satMix,
			                      &sound.shaper.threshold32Last, &sound.shaper.blendSlopeLast_Q8,
			                      0, // filterGain: 0 = FM mode (no gain compensation for now)
			                      sound.hasFilters(), &sound.shaper.prevScaledInputL, &sound.shaper.prevScaledInputR,
			                      &sound.shaper.prevSampleL, &sound.shaper.prevSampleR, &sound.shaper.zcCountL,
			                      &sound.shaper.zcCountR, &sound.shaper.subSignL, &sound.shaper.subSignR,
			                      sound.shaper.extrasMask, sound.shaper.gammaPhase, &sound.shaper.slewedL,
			                      &sound.shaper.slewedR, noteFreqHz);
		}

		// Filters
		filterSet.renderLongStereo(oscBuffer, oscBufferEnd);

		// No clipping
		if (!sound.clippingAmount) {

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
					outputSample->addPannedStereo(outputSampleL, outputSampleR, amplitudeL, amplitudeR);
				}
				else {
					outputSample->addStereo(outputSampleL, outputSampleR);
				}

				outputSample++;
			} while (oscBufferPos != oscBufferEnd);
		}

		// Yes clipping
		else {

			int32_t const* __restrict__ oscBufferPos = oscBuffer; // For traversal
			StereoSample* __restrict__ outputSample = (StereoSample*)soundBuffer;
			int32_t overallOscAmplitudeNow = overallOscAmplitudeLastTime;
			int32_t shiftAmount = sound.getShiftAmountForSaturation();

			do {
				int32_t outputSampleL = *(oscBufferPos++);
				int32_t outputSampleR = *(oscBufferPos++);

				overallOscAmplitudeNow += overallOscillatorAmplitudeIncrement;
				if (synthMode != SynthMode::FM) {
					outputSampleL = multiply_32x32_rshift32_rounded(outputSampleL, overallOscAmplitudeNow) << 1;
					outputSampleR = multiply_32x32_rshift32_rounded(outputSampleR, overallOscAmplitudeNow) << 1;
				}

				sound.saturate(&outputSampleL, &lastSaturationTanHWorkingValue[0], shiftAmount);
				sound.saturate(&outputSampleR, &lastSaturationTanHWorkingValue[1], shiftAmount);

				// Write to the output buffer, panning or not
				if (doPanning) {
					outputSample->addPannedStereo(outputSampleL, outputSampleR, amplitudeL, amplitudeR);
				}
				else {
					outputSample->addStereo(outputSampleL, outputSampleR);
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
		if (paramFinalValues[params::LOCAL_FOLD] > 0) {
			q31_t foldAmount = paramFinalValues[params::LOCAL_FOLD];

			dsp::foldBufferPolyApproximation(oscBuffer, oscBufferEnd, foldAmount);
		}

		// Sine Shaper (per-voice, mod-matrix routable drive, harmonic, and twist) - mono path
		if (sound.sineShaper.isEnabled()) {
			std::span<q31_t> monoBuffer(oscBuffer, numSamples);
			dsp::processSineShaper(monoBuffer, &sound.sineShaper, &sineShaperState,
			                       paramFinalValues[params::LOCAL_SINE_SHAPER_DRIVE],
			                       paramManager->getPatchedParamSet()->getValue(params::LOCAL_SINE_SHAPER_HARMONIC),
			                       paramFinalValues[params::LOCAL_SINE_SHAPER_HARMONIC],
			                       paramManager->getPatchedParamSet()->getValue(params::LOCAL_SINE_SHAPER_TWIST),
			                       paramFinalValues[params::LOCAL_SINE_SHAPER_TWIST], 0, // filterGain: 0 = FM mode
			                       sound.hasFilters());
		}

		// Table Shaper (per-voice, mono path)
		if (sound.shaper.shapeX > 0) {
			q31_t satDrive = paramFinalValues[params::LOCAL_TABLE_SHAPER_DRIVE];
			q31_t satMix = paramFinalValues[params::LOCAL_TABLE_SHAPER_MIX];
			// Convert buffer to q31_t span for mono processing
			std::span<q31_t> monoBuffer(oscBuffer, numSamples);
			// Compute note frequency for LPF cutoff scaling
			float noteFreqHz = 440.0f * powf(2.0f, (noteCodeAfterArpeggiation - 69) / 12.0f);
			// Use ShaperModState to group L-channel state for mono processing
			dsp::ShaperModState state{&sound.shaper.prevSampleL, &sound.shaper.slewedL, &sound.shaper.prevScaledInputL,
			                          &sound.shaper.zcCountL, &sound.shaper.subSignL};
			dsp::shapeBufferInt32(monoBuffer, sound.shaperDsp, satDrive, &sound.shaper.driveLast, satMix,
			                      &sound.shaper.threshold32Last, &sound.shaper.blendSlopeLast_Q8,
			                      0, // filterGain: 0 = FM mode (no gain compensation for now)
			                      sound.hasFilters(), state, sound.shaper.extrasMask, sound.shaper.gammaPhase,
			                      noteFreqHz);
		}

		filterSet.renderLong(oscBuffer, oscBufferEnd, numSamples);

		// No clipping
		if (!sound.clippingAmount) {
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
						((StereoSample*)outputSample)->addPannedMono(output, amplitudeL, amplitudeR);
					}
					else {
						((StereoSample*)outputSample)->addMono(output);
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

			auto clippingAmount = sound.getShiftAmountForSaturation();
			do {
				int32_t output = *oscBufferPos;

				if (synthMode != SynthMode::FM) {
					overallOscAmplitudeNow += overallOscillatorAmplitudeIncrement;
					output = multiply_32x32_rshift32_rounded(output, overallOscAmplitudeNow) << 1;
				}

				sound.saturate(&output, &lastSaturationTanHWorkingValue[0], clippingAmount);

				if (soundRenderingInStereo) {
					if (doPanning) {
						((StereoSample*)outputSample)->addPannedMono(output, amplitudeL, amplitudeR);
					}
					else {
						((StereoSample*)outputSample)->addMono(output);
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
		sourceWaveIndexesLastTime[s] = paramFinalValues[params::LOCAL_OSC_A_WAVE_INDEX + s];
	}
	for (int32_t m = 0; m < kNumModulators; m++) {
		modulatorAmplitudeLastTime[m] = paramFinalValues[params::LOCAL_MODULATOR_0_VOLUME + m];
	}
	overallOscAmplitudeLastTime = overallOscAmplitude;

	return !unassignVoiceAfter;
}

bool Voice::areAllUnisonPartsInactive(ModelStackWithSoundFlags& modelStack) const {
	// If no noise-source, then it might be time to unassign the voice...
	if (!modelStack.paramManager->getPatchedParamSet()->params[params::LOCAL_NOISE_VOLUME].containsSomething(
	        -2147483648)) {

		// See if all unison parts are now inactive
		for (int32_t s = 0; s < kNumSources; s++) {
			if (!modelStack.checkSourceEverActive(s)) {
				continue;
			}
			for (int32_t u = 0; u < ((Sound*)modelStack.modControllable)->numUnison; u++) {
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
		if (output >= (1 << 24)) {
			return false;
		}
		phaseIncrement = output << 8;
	}
	return true;
}

void Voice::renderSineWaveWithFeedback(int32_t* bufferStart, int32_t numSamples, uint32_t* phase, int32_t amplitude,
                                       uint32_t phaseIncrement, int32_t feedbackAmount, int32_t* lastFeedbackValue,
                                       bool add, int32_t amplitudeIncrement) {

	uint32_t phaseNow = *phase;
	*phase += phaseIncrement * numSamples;

	if (feedbackAmount) {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t feedbackValue = *lastFeedbackValue;
		int32_t* bufferEnd = bufferStart + numSamples;
		do {
			amplitudeNow += amplitudeIncrement;
			int32_t feedback = multiply_32x32_rshift32(feedbackValue, feedbackAmount);

			// We do hard clipping of the feedback amount. Doing tanH causes aliasing - even if we used the anti-aliased
			// version. The hard clipping one sounds really solid.
			feedback = signed_saturate<22>(feedback);

			feedbackValue = dsp::SineOsc::doFMNew(phaseNow += phaseIncrement, feedback);

			if (add) {
				*thisSample = multiply_accumulate_32x32_rshift32_rounded(*thisSample, feedbackValue, amplitudeNow);
			}
			else {
				*thisSample = multiply_32x32_rshift32(feedbackValue, amplitudeNow);
			}
		} while (++thisSample != bufferEnd);

		*lastFeedbackValue = feedbackValue;
	}
	else {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t* bufferEnd = bufferStart + numSamples;

		if (amplitudeIncrement) {
			do {
				int32x4_t sineValueVector = dsp::SineOsc::getSineVector(&phaseNow, phaseIncrement);

				int32x4_t amplitudeVector;
				for (int32_t i = 0; i < 4; i++) {
					amplitudeNow += amplitudeIncrement;
					amplitudeVector[i] = amplitudeNow >> 1;
				}

				int32x4_t resultValueVector = vqdmulhq_s32(amplitudeVector, sineValueVector);

				if (add) {
					int32x4_t existingBufferContents = vld1q_s32(thisSample);
					int32x4_t added = vaddq_s32(existingBufferContents, resultValueVector);
					vst1q_s32(thisSample, added);
				}
				else {
					vst1q_s32(thisSample, resultValueVector);
				}

				thisSample += 4;
			} while (thisSample < bufferEnd);
		}

		else {
			do {
				int32x4_t sineValueVector = dsp::SineOsc::getSineVector(&phaseNow, phaseIncrement);
				int32x4_t resultValueVector = vqrdmulhq_n_s32(sineValueVector, amplitudeNow >> 1);

				if (add) {
					int32x4_t existingBufferContents = vld1q_s32(thisSample);
					int32x4_t added = vaddq_s32(existingBufferContents, resultValueVector);
					vst1q_s32(thisSample, added);
				}
				else {
					vst1q_s32(thisSample, resultValueVector);
				}

				thisSample += 4;
			} while (thisSample < bufferEnd);
		}
	}
}

void Voice::renderFMWithFeedback(int32_t* bufferStart, int32_t numSamples, int32_t* fmBuffer, uint32_t* phase,
                                 int32_t amplitude, uint32_t phaseIncrement, int32_t feedbackAmount,
                                 int32_t* lastFeedbackValue, int32_t amplitudeIncrement) {

	uint32_t phaseNow = *phase;
	*phase += phaseIncrement * numSamples;

	if (feedbackAmount) {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t* bufferEnd = bufferStart + numSamples;

		int32_t feedbackValue = *lastFeedbackValue;
		do {
			amplitudeNow += amplitudeIncrement;

			int32_t feedback = multiply_32x32_rshift32(feedbackValue, feedbackAmount);

			// We do hard clipping of the feedback amount. Doing tanH causes aliasing - even if we used the anti-aliased
			// version. The hard clipping one sounds really solid.
			feedback = signed_saturate<22>(feedback);

			uint32_t sum = (uint32_t)*thisSample + (uint32_t)feedback;

			feedbackValue = dsp::SineOsc::doFMNew(phaseNow += phaseIncrement, sum);
			*thisSample = multiply_32x32_rshift32(feedbackValue, amplitudeNow);
		} while (++thisSample != bufferEnd);

		*lastFeedbackValue = feedbackValue;
	}
	else {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t* bufferEnd = bufferStart + numSamples;
		do {
			amplitudeNow += amplitudeIncrement;
			int32_t fmValue = dsp::SineOsc::doFMNew(phaseNow += phaseIncrement, *thisSample);
			*thisSample = multiply_32x32_rshift32(fmValue, amplitudeNow);
		} while (++thisSample != bufferEnd);
	}
}

void Voice::renderFMWithFeedbackAdd(int32_t* bufferStart, int32_t numSamples, int32_t* fmBuffer, uint32_t* phase,
                                    int32_t amplitude, uint32_t phaseIncrement, int32_t feedbackAmount,
                                    int32_t* lastFeedbackValue, int32_t amplitudeIncrement) {

	uint32_t phaseNow = *phase;
	*phase += phaseIncrement * numSamples;

	if (feedbackAmount) {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t* fmSample = fmBuffer;
		int32_t* bufferEnd = bufferStart + numSamples;

		int32_t feedbackValue = *lastFeedbackValue;
		do {
			amplitudeNow += amplitudeIncrement;

			int32_t feedback = multiply_32x32_rshift32(feedbackValue, feedbackAmount);

			// We do hard clipping of the feedback amount. Doing tanH causes aliasing - even if we used the anti-aliased
			// version. The hard clipping one sounds really solid.
			feedback = signed_saturate<22>(feedback);

			uint32_t sum = (uint32_t)*(fmSample++) + (uint32_t)feedback;

			feedbackValue = dsp::SineOsc::doFMNew(phaseNow += phaseIncrement, sum);
			*thisSample = multiply_accumulate_32x32_rshift32_rounded(*thisSample, feedbackValue, amplitudeNow);
		} while (++thisSample != bufferEnd);

		*lastFeedbackValue = feedbackValue;
	}
	else {
		int32_t amplitudeNow = amplitude;
		int32_t* thisSample = bufferStart;
		int32_t* bufferEnd = bufferStart + numSamples;
		uint32_t* fmSample = (uint32_t*)fmBuffer;
		int32_t* bufferPreEnd = bufferEnd - 4;

		uint32x4_t phaseVector;
		for (int32_t i = 0; i < 4; i++) {
			phaseNow += phaseIncrement;
			phaseVector[i] = phaseNow;
		}

		uint32x4_t phaseIncrementVector = vdupq_n_u32(phaseIncrement << 2);

		if (amplitudeIncrement) {
			while (true) {
				uint32x4_t phaseShift = vld1q_u32(fmSample);
				int32x4_t sineValueVector = dsp::SineOsc::doFMVector(phaseVector, phaseShift);

				int32x4_t amplitudeVector;
				for (int32_t i = 0; i < 4; i++) {
					amplitudeNow += amplitudeIncrement;
					amplitudeVector[i] = amplitudeNow >> 1;
				}

				int32x4_t resultValueVector = vqdmulhq_s32(amplitudeVector, sineValueVector);

				int32x4_t existingBufferContents = vld1q_s32(thisSample);
				int32x4_t added = vaddq_s32(existingBufferContents, resultValueVector);
				vst1q_s32(thisSample, added);

				if (thisSample >= bufferPreEnd) {
					break;
				}

				thisSample += 4;
				fmSample += 4;
				phaseVector = vaddq_u32(phaseVector, phaseIncrementVector);
			}
		}

		else {
			while (true) {
				uint32x4_t phaseShift = vld1q_u32(fmSample);
				int32x4_t sineValueVector = dsp::SineOsc::doFMVector(phaseVector, phaseShift);

				int32x4_t resultValueVector = vqrdmulhq_n_s32(sineValueVector, amplitudeNow >> 1);

				int32x4_t existingBufferContents = vld1q_s32(thisSample);
				int32x4_t added = vaddq_s32(existingBufferContents, resultValueVector);
				vst1q_s32(thisSample, added);

				if (thisSample >= bufferPreEnd) {
					break;
				}

				thisSample += 4;
				fmSample += 4;
				phaseVector = vaddq_u32(phaseVector, phaseIncrementVector);
			}
		}
	}
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

void Voice::renderBasicSource(Sound& sound, ParamManagerForTimeline* paramManager, int32_t s,
                              int32_t* __restrict__ oscBuffer, int32_t numSamples, bool stereoBuffer,
                              int32_t sourceAmplitude, bool* __restrict__ unisonPartBecameInactive,
                              int32_t overallPitchAdjust, bool doOscSync, uint32_t* __restrict__ oscSyncPos,
                              uint32_t* __restrict__ oscSyncPhaseIncrements, int32_t amplitudeIncrement,
                              uint32_t* __restrict__ getPhaseIncrements, bool getOutAfterPhaseIncrements,
                              int32_t waveIndexIncrement) {

	GeneralMemoryAllocator::get().checkStack("Voice::renderBasicSource");

	// Consume pre-roll silence for negative sample start offset.
	// Moves the audio start later in the buffer so the first N samples stay silent.
	if (sound.sources[s].oscType == OscType::SAMPLE && guides[s].preRollSamples > 0) {
		int32_t samplesToSkip = std::min(guides[s].preRollSamples, numSamples);
		guides[s].preRollSamples -= samplesToSkip;
		if (samplesToSkip >= numSamples) {
			return; // Entire block is silence
		}
		numSamples -= samplesToSkip;
		oscBuffer += samplesToSkip * (stereoBuffer ? 2 : 1);
		sourceAmplitude += amplitudeIncrement * samplesToSkip;
	}

	// For each unison part
	for (int32_t u = 0; u < sound.numUnison; u++) {

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
		if (!adjustPitch(phaseIncrement, paramFinalValues[params::LOCAL_OSC_A_PITCH_ADJUST + s])) {
			goto pitchTooHigh;
		}

		if (getPhaseIncrements) {
			getPhaseIncrements[u] = phaseIncrement;

			if (getOutAfterPhaseIncrements) {
				voiceUnisonPartSource->oscPos += phaseIncrement * numSamples;
				continue;
			}
		}

		bool stereoUnison = sound.unisonStereoSpread && sound.numUnison > 1 && stereoBuffer;
		int32_t amplitudeL, amplitudeR;
		shouldDoPanning((stereoUnison ? sound.unisonPan[u] : 0), &amplitudeL, &amplitudeR);
		// used if mono source but stereoUnison active

		// If sample...
		if (sound.sources[s].oscType == OscType::SAMPLE) {

			Sample* sample = (Sample*)guides[s].audioFileHolder->audioFile;
			VoiceSample* voiceSample = voiceUnisonPartSource->voiceSample;

			int32_t numChannels = (sample->numChannels == 2) ? 2 : 1;

#ifdef TEST_SAMPLE_LOOP_POINTS
			if (!(getNoise() >> 19)) {
				// D_PRINTLN("random change");

				int32_t r = getRandom255();

				if (r < 128) {
					sound.guides[s].timeStretchAmount = (getRandom255() % 24) - 12;
				}

				else {
					if ((guides[s].audioFileHolder->transpose || guides[s].audioFileHolder->cents)
					    && getRandom255() < 128) {
						guides[s].audioFileHolder->transpose = 0;
						guides[s].audioFileHolder->setCents(0);
					}

					else {
						guides[s].audioFileHolder->transpose = (int32_t)(getRandom255() % 24) - 12;
						guides[s].audioFileHolder->setCents((int32_t)(getRandom255() % 100) - 50);
					}

					sound.recalculateAllVoicePhaseIncrements(paramManager);
				}

				// D_PRINTLN("end random change");
			}
#endif

			// First figure out the time-stretching amount

			int32_t pitchAdjustNeutralValue = ((SampleHolder*)guides[s].audioFileHolder)->neutralPhaseIncrement;
			uint32_t timeStretchRatio;
			uint32_t noteLengthInSamples;

			bool stillOk = voiceUnisonPartSource->getPitchAndSpeedParams(&sound.sources[s], &guides[s], &phaseIncrement,
			                                                             &timeStretchRatio, &noteLengthInSamples);
			if (!stillOk) {
				goto instantUnassign;
			}

			// If user unmuted mid-note...
			bool tryToStartMidNote = voiceSample->pendingSamplesLate;
			if (tryToStartMidNote) [[unlikely]] {

				int32_t rawSamplesLate;

				// Synced / STRETCH / PHASE_LOCKED with sync - it's super easy.
				if ((sound.sources[s].repeatMode == SampleRepeatMode::STRETCH
				     || sound.sources[s].repeatMode == SampleRepeatMode::PHASE_LOCKED)
				    && guides[s].sequenceSyncLengthTicks) {
					rawSamplesLate = guides[s].getSyncedNumSamplesIn();
				}

				// Or, normal - it needs a bit more explanation.
				else {
					// We have to ignore any pitch modulation, aka the "patched" value for phaseIncrement: we have to
					// use phaseIncrementStoredValue instead, which is the pre-modulation/patching value. But we also
					// have to forget about the timeStretchRatio we calculated above with the call to
					// getPitchAndSpeedParams(), and instead calculate a special version of this with this call to
					// getSpeedParamForNoSyncing(), which is what getPitchAndSpeedParams() itself calls when not in
					// STRETCH mode, which we've already determined we're not in, and crucially pass it the not-patched
					// voiceUnisonPartSource->phaseIncrementStoredValue. This fix was done in September 2020 after bug
					// report from Clyde.
					uint32_t timeStretchRatioWithoutModulation = voiceUnisonPartSource->getSpeedParamForNoSyncing(
					    &sound.sources[s], voiceUnisonPartSource->phaseIncrementStoredValue,
					    ((SampleHolder*)guides[s].audioFileHolder)->neutralPhaseIncrement);

					// Cool, so now we've got phaseIncrement and timeStretchRatio equivalent values which will indicate
					// our correct play position into the sample regardless of pitch modulation (almost always vibrato).
					rawSamplesLate =
					    ((((uint64_t)voiceSample->pendingSamplesLate * voiceUnisonPartSource->phaseIncrementStoredValue)
					      >> 24)
					     * timeStretchRatioWithoutModulation)
					    >> 24;
				}

				LateStartAttemptStatus result =
				    voiceSample->attemptLateSampleStart(&guides[s], sample, rawSamplesLate, numSamples);

				if (result == LateStartAttemptStatus::FAILURE) {
					goto instantUnassign;
				}
				else if (result == LateStartAttemptStatus::WAIT) {
					continue;
				}
				// Otherwise, it started fine!
			}

			LoopType loopingType = guides[s].getLoopingType(sound.sources[s]);

			int32_t interpolationBufferSize;

			// If pitch adjustment...
			if (phaseIncrement != kMaxSampleValue) {

				// Work out what quality we're going to do that at
				interpolationBufferSize = sound.sources[s].sampleControls.getInterpolationBufferSize(phaseIncrement);

				// And if first render, and other conditions met, see if we can use cache.
				// It may seem like it'd be a good idea to try and set this up on note-on, rather than here in the
				// rendering routine, but I tried that and the fact is that it means a bunch of extra computation has to
				// happen to work out pitch and timestretch there as well as here (where it'll be worked out anyway),
				// including checking the result of patching / modulation (and we *do* allow caching where, say,
				// velocity or note is affecting pitch), and stretch-syncing.
				if (!voiceSample->doneFirstRenderYet && !tryToStartMidNote
				    && portaEnvelopePos == 0xFFFFFFFF) { // No porta

					// If looping, make sure the loop isn't too short. If so, caching just wouldn't sound good /
					// accurate
					if (loopingType != LoopType::NONE) {
						SampleHolderForVoice* holder = (SampleHolderForVoice*)guides[s].audioFileHolder;
						int32_t loopStart = holder->loopStartPos ? holder->loopStartPos : holder->startPos;
						int32_t loopEnd = holder->loopEndPos ? holder->loopEndPos : holder->endPos;

						int32_t loopLength = loopEnd - loopStart;
						loopLength = std::abs(loopLength);
						uint64_t phaseIncrementTimesTimeStretchRatio =
						    ((uint64_t)(uint32_t)phaseIncrement * (uint32_t)timeStretchRatio) >> 24;
						int32_t loopLengthCached =
						    ((uint64_t)(uint32_t)loopLength << 24) / phaseIncrementTimesTimeStretchRatio;
						if (loopLengthCached < 2205) {
							goto dontUseCache; // Limit is 50mS i.e. 20hZ
						}
					}

					// If no changeable sources patched to pitch...
					for (int32_t c = 0; c < paramManager->getPatchCableSet()->numUsablePatchCables; c++) {
						PatchCable* cable = &paramManager->getPatchCableSet()->patchCables[c];

						// If it's going to pitch...
						if (cable->destinationParamDescriptor.isSetToParamWithNoSource(params::LOCAL_PITCH_ADJUST)
						    || cable->destinationParamDescriptor.isSetToParamWithNoSource(
						        params::LOCAL_OSC_A_PITCH_ADJUST + s)) {

							// And if it's an envelope or LFO or random...
							if (cable->from == PatchSource::ENVELOPE_0 || cable->from == PatchSource::ENVELOPE_1
							    || cable->from == PatchSource::LFO_GLOBAL_1 || cable->from == PatchSource::LFO_LOCAL_1
							    || cable->from == PatchSource::LFO_GLOBAL_2 || cable->from == PatchSource::LFO_LOCAL_2
							    || cable->from == PatchSource::RANDOM) {
								goto dontUseCache;
							}

							else if (cable->from == PatchSource::AFTERTOUCH) {
								if (sourceValues[util::to_underlying(PatchSource::AFTERTOUCH)]) {
									goto dontUseCache;
								}
							}

							// TODO: probably need to check for X and Y modulation sources here too...

							else if (cable->from == PatchSource::SIDECHAIN) {
								if (sound.globalSourceValues[util::to_underlying(PatchSource::SIDECHAIN)]) {
									goto dontUseCache;
								}
							}
						}
					}

					{
						// If still here, we can use cache
						bool everythingOk = voiceSample->possiblySetUpCache(
						    &sound.sources[s].sampleControls, &guides[s], phaseIncrement, timeStretchRatio,
						    getPriorityRating(), loopingType);
						if (!everythingOk) {
							goto instantUnassign;
						}
					}

dontUseCache: {}
				}
			}

			int32_t* renderBuffer = oscBuffer;

			if (stereoUnison) [[unlikely]] {
				// TODO: I first wanted to integrate this with voiceSample->render()'s own
				// amplitude control but it is just too complex - multiple copies of
				// the amp logic depending if caching is used or not, timestretching or not,
				// for now settle for "don't pay what you don't use", i e no extra copies/maths
				// if you don't use unison stereo on a sample - bfredl
				renderBuffer = spareRenderingBuffer[2]; // note: 2 and 3 are used
				memset(renderBuffer, 0, 2 * SSI_TX_BUFFER_NUM_SAMPLES * sizeof(int32_t));
			}

			// We no longer do caching when there's just time stretching with no pitch adjustment, because the time
			// stretching algorithm is so efficient, playing back the cache is hardly any faster than just doing the
			// time stretching (once perc info has been cached) - and, crucially, creating / writing to the cache in the
			// first place is quite inefficient when time stretching, because when we're not writing to the cache, that
			// allows us to do a special optimization not otherwise available (that is, combining the amplitude
			// increments for the hop crossfades with the overall voice ones, and having multiple crossfading hops write
			// directly to the osc buffer).

			bool stillActive = voiceSample->render(
			    &guides[s], renderBuffer, numSamples, sample, numChannels, loopingType, phaseIncrement,
			    timeStretchRatio, sourceAmplitude, amplitudeIncrement, interpolationBufferSize,
			    sound.sources[s].sampleControls.interpolationMode, getPriorityRating());

			if (stereoUnison) {
				if (numChannels == 2) {
					// TODO: society if renderBasicSource() took a StereoSample[] buffer already
					for (int32_t i = 0; i < numSamples; i++) {
						oscBuffer[(i << 1)] += multiply_32x32_rshift32(renderBuffer[(i << 1)], amplitudeL) << 2;
						oscBuffer[(i << 1) + 1] += multiply_32x32_rshift32(renderBuffer[(i << 1) + 1], amplitudeR) << 2;
					}
				}
				else {
					// TODO: if render buffer was typed we could use addPannedMono()
					for (int32_t i = 0; i < numSamples; i++) {
						oscBuffer[(i << 1)] += multiply_32x32_rshift32(renderBuffer[i], amplitudeL) << 2;
						oscBuffer[(i << 1) + 1] += multiply_32x32_rshift32(renderBuffer[i], amplitudeR) << 2;
					}
				}
			}

			if (!stillActive) {
				goto instantUnassign;
			}
		}

		// Or echoing input
		else if (sound.sources[s].oscType == OscType::INPUT_L || sound.sources[s].oscType == OscType::INPUT_R
		         || sound.sources[s].oscType == OscType::INPUT_STEREO) {

			VoiceUnisonPartSource* source = &unisonParts[u].sources[s];
			OscType inputTypeNow = sound.sources[s].oscType;
			// If pitch shifting and we weren't previously...
			if (phaseIncrement != kMaxSampleValue) {

				if (!source->livePitchShifter) {

					if (inputTypeNow == OscType::INPUT_STEREO && !AudioEngine::lineInPluggedIn
					    && !AudioEngine::micPluggedIn) {
						inputTypeNow = OscType::INPUT_L;
					}

					LiveInputBuffer* liveInputBuffer = AudioEngine::getOrCreateLiveInputBuffer(inputTypeNow, true);

					if (liveInputBuffer) {

						void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(LivePitchShifter));

						if (memory) {
							source->livePitchShifter = new (memory) LivePitchShifter(inputTypeNow, phaseIncrement);
							D_PRINTLN("start pitch shifting");
						}
					}
				}
			}

			// If not pitch shifting and we were previously...
			else {
				if (source->livePitchShifter && source->livePitchShifter->mayBeRemovedWithoutClick()) {
					D_PRINTLN("stop pitch shifting");
					source->livePitchShifter->~LivePitchShifter();
					delugeDealloc(source->livePitchShifter);
					source->livePitchShifter = nullptr;
				}
			}

			// Yes pitch shifting
			if (source->livePitchShifter) {
				int32_t interpolationBufferSize =
				    sound.sources[s].sampleControls.getInterpolationBufferSize(phaseIncrement);

				source->livePitchShifter->render(oscBuffer, numSamples, phaseIncrement, sourceAmplitude,
				                                 amplitudeIncrement, interpolationBufferSize);
			}

			// No pitch shifting
			else {

				int32_t* __restrict__ oscBufferPos = oscBuffer;
				int32_t const* __restrict__ inputReadPos = (int32_t const*)AudioEngine::i2sRXBufferPos;
				int32_t sourceAmplitudeThisUnison = sourceAmplitude;
				int32_t amplitudeIncrementThisUnison = amplitudeIncrement;

				// Just left, or just right, or if (stereo but there's only the internal, mono mic)
				if (sound.sources[s].oscType != OscType::INPUT_STEREO
				    || (!AudioEngine::lineInPluggedIn && !AudioEngine::micPluggedIn)) {

					int32_t const* const oscBufferEnd = oscBuffer + numSamples;

					int32_t channelOffset;
					// If right, but not internal mic
					if (sound.sources[s].oscType == OscType::INPUT_R
					    && (AudioEngine::lineInPluggedIn || AudioEngine::micPluggedIn)) {
						channelOffset = 1;

						// Or if left or using internal mic
					}
					else {
						channelOffset = 0;
					}

					int32_t sourceAmplitudeNow = sourceAmplitudeThisUnison;
					do {
						sourceAmplitudeNow += amplitudeIncrement;

						// Mono / left channel (or stereo condensed to mono)
						*(oscBufferPos++) += multiply_32x32_rshift32(inputReadPos[channelOffset], sourceAmplitudeNow)
						                     << 4;

						inputReadPos += NUM_MONO_INPUT_CHANNELS;
						if (inputReadPos >= getRxBufferEnd()) {
							inputReadPos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
						}
					} while (oscBufferPos != oscBufferEnd);
				}

				// Stereo
				else {

					int32_t numChannelsAfterCondensing = stereoBuffer ? 2 : 1;

					int32_t const* const oscBufferEnd = oscBuffer + numSamples * numChannelsAfterCondensing;

					int32_t sourceAmplitudeNow = sourceAmplitudeThisUnison;
					do {
						sourceAmplitudeNow += amplitudeIncrement;

						int32_t sampleL = inputReadPos[0];
						int32_t sampleR = inputReadPos[1];

						// If condensing to mono, do that now
						if (!stereoBuffer) {
							sampleL = ((sampleL >> 1) + (sampleR >> 1));
						}

						// Mono / left channel (or stereo condensed to mono)
						*(oscBufferPos++) += multiply_32x32_rshift32(sampleL, sourceAmplitudeNow) << 4;

						// Right channel
						if (stereoBuffer) {
							*(oscBufferPos++) += multiply_32x32_rshift32(sampleR, sourceAmplitudeNow) << 4;
						}

						inputReadPos += NUM_MONO_INPUT_CHANNELS;
						if (inputReadPos >= getRxBufferEnd()) {
							inputReadPos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
						}
					} while (oscBufferPos != oscBufferEnd);
				}
			}
		}
		else if (sound.sources[s].oscType == OscType::DX7) {
			static int32_t uniBuf[DX_MAX_N] __attribute__((aligned(CACHE_LINE_SIZE)));
			memset(uniBuf, 0, sizeof uniBuf);

			// TODO: 1. use existing int log function?
			//       2. going from phase to logs (and then let MSFA turn those logs into phase again) is sus af
			//         rework MSFA to use our phase incerements directly?
			int logpitch = (int)(log2f(phaseIncrement) * (1 << 24));
			int adjpitch = logpitch - 278023814;

			DxPatch* patch = sound.sources[s].ensureDxPatch();
			DxVoiceCtrl ctrl{};
			ctrl.ampmod = paramFinalValues[params::LOCAL_OSC_A_PHASE_WIDTH + s] >> 13;
			// ctrl.ratemod = paramFinalValues[params::LOCAL_CARRIER_0_FEEDBACK + s] >> 16;
			if (sound.sources[s].dxPatchChanged) {
				unisonParts[u].sources[s].dxVoice->update(*patch, noteCodeAfterArpeggiation);
			}
			bool active = unisonParts[u].sources[s].dxVoice->compute(uniBuf, numSamples, adjpitch, patch, &ctrl);
			if (!active) {
				goto instantUnassign;
			}

			int32_t sourceAmplitudeNow = sourceAmplitude;
			if (stereoUnison) {
				for (int i = 0; i < numSamples; i++) {
					sourceAmplitudeNow += amplitudeIncrement;
					int amplified = multiply_32x32_rshift32(uniBuf[i], sourceAmplitudeNow) << 6;
					oscBuffer[(i << 1)] += multiply_32x32_rshift32(amplified, amplitudeL) << 2;
					oscBuffer[(i << 1) + 1] += multiply_32x32_rshift32(amplified, amplitudeR) << 2;
				}
			}
			else {
				for (int i = 0; i < numSamples; i++) {
					sourceAmplitudeNow += amplitudeIncrement;
					oscBuffer[i] += multiply_32x32_rshift32(uniBuf[i], sourceAmplitudeNow) << 6;
				}
			}

			// Or regular wave
		}
		else [[likely]] {
			uint32_t oscSyncPosThisUnison;
			uint32_t oscSyncPhaseIncrementsThisUnison;
			uint32_t oscRetriggerPhase =
			    sound.oscRetriggerPhase[s]; // Yes we might need this even if not doing osc sync.

			// If doing osc sync
			if (doOscSync) {

				// If freq too high...
				if (!oscSyncPhaseIncrements[u]) {
					continue;
				}

				oscSyncPosThisUnison = oscSyncPos[u];
				oscSyncPhaseIncrementsThisUnison = oscSyncPhaseIncrements[u];
			}

			int32_t* renderBuffer = oscBuffer;

			if (stereoBuffer) {
				renderBuffer = spareRenderingBuffer[2];
				memset(renderBuffer, 0, SSI_TX_BUFFER_NUM_SAMPLES * sizeof(int32_t));
			}

			int32_t* oscBufferEnd =
			    renderBuffer + numSamples; // TODO: we don't really want to be calculating this so early do we?

			// Work out pulse width
			uint32_t pulseWidth = (uint32_t)lshiftAndSaturate<1>(paramFinalValues[params::LOCAL_OSC_A_PHASE_WIDTH + s]);

			dsp::Oscillator::renderOsc(
			    sound.sources[s].oscType, sourceAmplitude, renderBuffer, oscBufferEnd, numSamples, phaseIncrement,
			    pulseWidth, &unisonParts[u].sources[s].oscPos, true, amplitudeIncrement, doOscSync,
			    oscSyncPosThisUnison, oscSyncPhaseIncrementsThisUnison, oscRetriggerPhase, waveIndexIncrement,
			    sourceWaveIndexesLastTime[s], static_cast<WaveTable*>(guides[s].audioFileHolder->audioFile));

			if (stereoBuffer) {
				// TODO: if render buffer was typed we could use addPannedMono()
				for (int32_t i = 0; i < numSamples; i++) {
					oscBuffer[(i << 1)] += multiply_32x32_rshift32(renderBuffer[i], amplitudeL) << 2;
					oscBuffer[(i << 1) + 1] += multiply_32x32_rshift32(renderBuffer[i], amplitudeR) << 2;
				}
			}
		}
	}
}

bool Voice::doFastRelease(uint32_t releaseIncrement) {
	if (doneFirstRender) {
		envelopes[0].unconditionalRelease(EnvelopeStage::FAST_RELEASE, releaseIncrement);
		return true;
	}

	// Or if first render not done yet, we actually don't want to hear anything at all, so just unassign it
	else {
		return false;
	}
}

bool Voice::forceNormalRelease() {
	if (doneFirstRender) {
		// If no release-stage, we'll stop as soon as we can
		if (!hasReleaseStage()) {
			envelopes[0].unconditionalRelease(EnvelopeStage::FAST_RELEASE, SOFT_CULL_INCREMENT);
		}
		// otherwise we'll just let it get there on its own
		else {
			envelopes[0].unconditionalRelease();
		}
		return true;
	}

	// Or if first render not done yet, we actually don't want to hear anything at all, so just unassign it
	else {
		return false;
	}
}

bool Voice::speedUpRelease() {
	auto stage = envelopes[0].state;
	if (stage == EnvelopeStage::RELEASE) {
		auto currentRelease = paramFinalValues[params::LOCAL_ENV_0_RELEASE];
		envelopes[0].unconditionalRelease(EnvelopeStage::FAST_RELEASE, currentRelease * 2);
		return true;
	}
	else if (stage == EnvelopeStage::FAST_RELEASE) {
		auto currentRelease = envelopes[0].fastReleaseIncrement;
		envelopes[0].unconditionalRelease(EnvelopeStage::FAST_RELEASE, currentRelease * 2);
		return true;
	}

	// Or if we're not releasing just start the release
	else {
		return forceNormalRelease();
	}
}

bool Voice::doImmediateRelease() {
	if (doneFirstRender) {
		envelopes[0].unconditionalOff();
		return true;
	}

	// Or if first render not done yet, we actually don't want to hear anything at all, so just unassign it
	return false;
}

bool Voice::hasReleaseStage() {
	return (paramFinalValues[params::LOCAL_ENV_0_RELEASE] <= 18359);
}

// if more added, need to change the shifts around
static_assert(kNumEnvelopeStages < 8, "Too many envelope stages");
static_assert(kNumVoicePriorities < 4, "Too many priority options");

// Higher numbers are lower priority. 1 is top priority. Will never return 0, because nextVoiceState starts at 1
uint32_t Voice::getPriorityRating() const {
	return
	    // Bits 30-31 - manual priority setting
	    ((uint32_t)(3 - util::to_underlying(sound.voicePriority)) << 30)

	    // Bits 27-29 - how many voices that Sound has
	    // - that one really does need to go above state, otherwise "once" samples can still cut out synth drones.
	    // In a perfect world, culling for the purpose of "soliciting" a Voice would also count the new Voice being
	    // solicited, preferring to cut out that same Sound's old, say, one Voice, than another Sound's only Voice
	    + ((uint32_t)std::min<size_t>(sound.numActiveVoices(), 7) << 27)

	    // Bits 24-26 - envelope state
	    + ((uint32_t)envelopes[0].state << 24)

	    // Bits  0-23 - time entered
	    + ((uint32_t)(-envelopes[0].timeEnteredState) & (0xFFFFFFFF >> 8));
}
