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
#include "dsp/filter/filter_set.h"
#include "model/voice/voice_sample_playback_guide.h"
#include "model/voice/voice_unison_part.h"
#include "modulation/envelope.h"
#include "modulation/lfo.h"
#include "modulation/params/param.h"
#include "modulation/patch/patcher.h"

class StereoSample;
class ModelStackWithVoice;
using namespace deluge;
class Voice final {
public:
	Voice();

	Patcher patcher;

	// Stores all oscillator positions and stuff, for each Source within each Unison too
	VoiceUnisonPart unisonParts[kMaxNumVoicesUnison];

	// Stores overall info on each Source (basically just sample memory bounds), for the play-through associated with
	// this Voice right now.
	VoiceSamplePlaybackGuide guides[kNumSources];

	Sound* assignedToSound;

	///
	/// This is just for the *local* params, specific to this Voice only
	///
	int32_t paramFinalValues[deluge::modulation::params::LOCAL_LAST];

	// At the start of this list are local copies of the "global" ones. It's cheaper to copy them here than to pick and
	// choose where the Patcher looks for them
	int32_t sourceValues[kNumPatchSources];

	int32_t localExpressionSourceValuesBeforeSmoothing[kNumExpressionDimensions];

	Envelope envelopes[kNumEnvelopes];
	LFO lfo2;
	LFO lfo4;

	dsp::filter::FilterSet filterSet;
	int32_t inputCharacteristics[2]; // Contains what used to be called noteCodeBeforeArpeggiation, and fromMIDIChannel
	int32_t noteCodeAfterArpeggiation;

	uint32_t portaEnvelopePos;
	int32_t portaEnvelopeMaxAmplitude;

	uint32_t lastSaturationTanHWorkingValue[2];

	int32_t overallOscAmplitudeLastTime;
	int32_t sourceAmplitudesLastTime[kNumSources];
	int32_t modulatorAmplitudeLastTime[kNumModulators];
	uint32_t sourceWaveIndexesLastTime[kNumSources];

	int32_t filterGainLastTime;

	bool doneFirstRender;
	bool previouslyIgnoredNoteOff;
	uint8_t whichExpressionSourcesCurrentlySmoothing;
	uint8_t whichExpressionSourcesFinalValueChanged;

	uint32_t orderSounded;

	int32_t overrideAmplitudeEnvelopeReleaseRate;

	Voice* nextUnassigned;
	bool justCreated{false};

	uint32_t getLocalLFOPhaseIncrement(LFO_ID lfoId, deluge::modulation::params::Local param);
	void setAsUnassigned(ModelStackWithVoice* modelStack, bool deletingSong = false);
	bool render(ModelStackWithVoice* modelStack, int32_t* soundBuffer, int32_t numSamples, bool soundRenderingInStereo,
	            bool applyingPanAtVoiceLevel, uint32_t sourcesChanged, bool doLPF, bool doHPF,
	            int32_t externalPitchAdjust);

	void calculatePhaseIncrements(ModelStackWithVoice* modelStack);
	bool sampleZoneChanged(ModelStackWithVoice* modelStack, int32_t s, MarkerType markerType);
	bool noteOn(ModelStackWithVoice* modelStack, int32_t newNoteCodeBeforeArpeggiation,
	            int32_t newNoteCodeAfterArpeggiation, uint8_t velocity, uint32_t newSampleSyncLength, int32_t ticksLate,
	            uint32_t samplesLate, bool resetEnvelopes, int32_t fromMIDIChannel, const int16_t* mpeValues);
	void noteOff(ModelStackWithVoice* modelStack, bool allowReleaseStage = true);

	void randomizeOscPhases(Sound* sound);
	void changeNoteCode(ModelStackWithVoice* modelStack, int32_t newNoteCodeBeforeArpeggiation,
	                    int32_t newNoteCodeAfterArpeggiation, int32_t newInputMIDIChannel, const int16_t* newMPEValues);
	bool hasReleaseStage();
	void unassignStuff(bool deletingSong);
	uint32_t getPriorityRating();
	void expressionEventImmediate(Sound* sound, int32_t voiceLevelValue, int32_t s);
	void expressionEventSmooth(int32_t newValue, int32_t s);

	/// Release immediately with provided release rate
	/// Returns whether voice should still be left active
	bool doFastRelease(uint32_t releaseIncrement = 4096);

	/// Sets envelope to off (will interpolate through this render window).
	/// Returns whether voice should still be left active
	bool doImmediateRelease();

	bool forceNormalRelease();

	bool speedUpRelease();

private:
	// inline int32_t doFM(uint32_t *carrierPhase, uint32_t* lastShiftedPhase, uint32_t carrierPhaseIncrement, uint32_t
	// phaseShift);

	void renderOsc(int32_t s, OscType type, int32_t amplitude, int32_t* thisSample, int32_t* bufferEnd,
	               int32_t numSamples, uint32_t phaseIncrementNow, uint32_t phaseWidth, uint32_t* thisPhase,
	               bool applyAmplitude, int32_t amplitudeIncrement, bool doOscSync, uint32_t resetterPhase,
	               uint32_t resetterPhaseIncrement, uint32_t retriggerPhase, int32_t waveIndexIncrement);
	void renderBasicSource(Sound* sound, ParamManagerForTimeline* paramManager, int32_t s, int32_t* oscBuffer,
	                       int32_t numSamples, bool stereoBuffer, int32_t sourceAmplitude,
	                       bool* unisonPartBecameInactive, int32_t overallPitchAdjust, bool doOscSync,
	                       uint32_t* oscSyncPos, uint32_t* oscSyncPhaseIncrements, int32_t amplitudeIncrement,
	                       uint32_t* getPhaseIncrements, bool getOutAfterPhaseIncrements, int32_t waveIndexIncrement);
	bool adjustPitch(uint32_t* phaseIncrement, int32_t adjustment);

	void renderSineWaveWithFeedback(int32_t* thisSample, int32_t numSamples, uint32_t* phase, int32_t amplitude,
	                                uint32_t phaseIncrement, int32_t feedbackAmount, int32_t* lastFeedbackValue,
	                                bool add, int32_t amplitudeIncrement);
	void renderFMWithFeedback(int32_t* thisSample, int32_t numSamples, int32_t* fmBuffer, uint32_t* phase,
	                          int32_t amplitude, uint32_t phaseIncrement, int32_t feedbackAmount,
	                          int32_t* lastFeedbackValue, int32_t amplitudeIncrement);
	void renderFMWithFeedbackAdd(int32_t* thisSample, int32_t numSamples, int32_t* fmBuffer, uint32_t* phase,
	                             int32_t amplitude, uint32_t phaseIncrement, int32_t feedbackAmount,
	                             int32_t* lastFeedbackValue, int32_t amplitudeIncrement);
	bool areAllUnisonPartsInactive(ModelStackWithVoice* modelStackWithVoice);
	void setupPorta(Sound* sound);
	int32_t combineExpressionValues(Sound* sound, int32_t whichExpressionDimension);
};
