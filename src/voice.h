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

#ifndef VOICE_H
#define VOICE_H

#include <patcher.h>
#include <voicesampleplaybackguide.h>
#include "definitions.h"
#include "envelope.h"
#include "lfo.h"
#include "voiceunisonpart.h"
#include "FilterSet.h"

class StereoSample;
class FilterSetConfig;
class ModelStackWithVoice;

class Voice final {
public:
	Voice();

	Patcher patcher;

	VoiceUnisonPart
	    unisonParts[maxNumUnison]; // Stores all oscillator positions and stuff, for each Source within each Unison too
	VoiceSamplePlaybackGuide guides
	    [NUM_SOURCES]; // Stores overall info on each Source (basically just sample memory bounds), for the play-through associated with this Voice right now.

	Sound* assignedToSound;

	int32_t paramFinalValues[FIRST_GLOBAL_PARAM]; // This is just for the *local* params, specific to this Voice only
	int32_t sourceValues
	    [NUM_PATCH_SOURCES]; // At the start of this list are local copies of the "global" ones. It's cheaper to copy them here than to pick and choose where the Patcher looks for them

	int32_t localExpressionSourceValuesBeforeSmoothing[NUM_EXPRESSION_DIMENSIONS];

	Envelope envelopes[numEnvelopes];
	LFO lfo;

	FilterSet filterSets[2];
	int inputCharacteristics[2]; // Contains what used to be called noteCodeBeforeArpeggiation, and fromMIDIChannel
	int noteCodeAfterArpeggiation;

	uint32_t portaEnvelopePos;
	int32_t portaEnvelopeMaxAmplitude;

	uint32_t lastSaturationTanHWorkingValue[2];

	int32_t overallOscAmplitudeLastTime;
	int32_t sourceAmplitudesLastTime[NUM_SOURCES];
	int32_t modulatorAmplitudeLastTime[numModulators];
	uint32_t sourceWaveIndexesLastTime[NUM_SOURCES];

	int32_t filterGainLastTime;

	bool doneFirstRender;
	bool previouslyIgnoredNoteOff;
	uint8_t whichExpressionSourcesCurrentlySmoothing;
	uint8_t whichExpressionSourcesFinalValueChanged;

	uint32_t orderSounded;

	int32_t overrideAmplitudeEnvelopeReleaseRate;

	Voice* nextUnassigned;

	void setAsUnassigned(ModelStackWithVoice* modelStack, bool deletingSong = false);
	bool render(ModelStackWithVoice* modelStack, int32_t* soundBuffer, int numSamples, bool soundRenderingInStereo,
	            bool applyingPanAtVoiceLevel, uint32_t sourcesChanged, FilterSetConfig* filterSetConfig,
	            int32_t externalPitchAdjust);

	void calculatePhaseIncrements(ModelStackWithVoice* modelStack);
	bool sampleZoneChanged(ModelStackWithVoice* modelStack, int s, int markerType);
	bool noteOn(ModelStackWithVoice* modelStack, int newNoteCodeBeforeArpeggiation, int newNoteCodeAfterArpeggiation,
	            uint8_t velocity, uint32_t newSampleSyncLength, int32_t ticksLate, uint32_t samplesLate,
	            bool resetEnvelopes, int fromMIDIChannel, const int16_t* mpeValues);
	void noteOff(ModelStackWithVoice* modelStack, bool allowReleaseStage = true);
	bool doFastRelease(uint32_t releaseIncrement = 4096);
	void randomizeOscPhases(Sound* sound);
	void changeNoteCode(ModelStackWithVoice* modelStack, int newNoteCodeBeforeArpeggiation,
	                    int newNoteCodeAfterArpeggiation, int newInputMIDIChannel, const int16_t* newMPEValues);
	bool hasReleaseStage();
	void unassignStuff();
	uint32_t getPriorityRating();
	void expressionEventImmediate(Sound* sound, int32_t voiceLevelValue, int s);
	void expressionEventSmooth(int32_t newValue, int s);

private:
	//inline int32_t doFM(uint32_t *carrierPhase, uint32_t* lastShiftedPhase, uint32_t carrierPhaseIncrement, uint32_t phaseShift);

	void renderOsc(int s, int type, int32_t amplitude, int32_t* thisSample, int32_t* bufferEnd, int numSamples,
	               uint32_t phaseIncrementNow, uint32_t phaseWidth, uint32_t* thisPhase, bool applyAmplitude,
	               int32_t amplitudeIncrement, bool doOscSync, uint32_t resetterPhase, uint32_t resetterPhaseIncrement,
	               uint32_t retriggerPhase, int32_t waveIndexIncrement);
	void renderBasicSource(Sound* sound, ParamManagerForTimeline* paramManager, int s, int32_t* oscBuffer,
	                       int numSamples, int32_t sourceAmplitude, bool* unisonPartBecameInactive,
	                       int32_t overallPitchAdjust, bool doOscSync, uint32_t* oscSyncPos,
	                       uint32_t* oscSyncPhaseIncrements, int32_t amplitudeIncrement, uint32_t* getPhaseIncrements,
	                       bool getOutAfterPhaseIncrements, int32_t waveIndexIncrement);
	bool adjustPitch(uint32_t* phaseIncrement, int32_t adjustment);

	void renderSineWaveWithFeedback(int32_t* thisSample, int numSamples, uint32_t* phase, int32_t amplitude,
	                                uint32_t phaseIncrement, int32_t feedbackAmount, int32_t* lastFeedbackValue,
	                                bool add, int32_t amplitudeIncrement);
	void renderFMWithFeedback(int32_t* thisSample, int numSamples, int32_t* fmBuffer, uint32_t* phase,
	                          int32_t amplitude, uint32_t phaseIncrement, int32_t feedbackAmount,
	                          int32_t* lastFeedbackValue, int32_t amplitudeIncrement);
	void renderFMWithFeedbackAdd(int32_t* thisSample, int numSamples, int32_t* fmBuffer, uint32_t* phase,
	                             int32_t amplitude, uint32_t phaseIncrement, int32_t feedbackAmount,
	                             int32_t* lastFeedbackValue, int32_t amplitudeIncrement);
	bool areAllUnisonPartsInactive(ModelStackWithVoice* modelStackWithVoice);
	void setupPorta(Sound* sound);
	int32_t combineExpressionValues(Sound* sound, int whichExpressionDimension);
};

#endif // VOICE_H
