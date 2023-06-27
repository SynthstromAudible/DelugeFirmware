/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#ifndef MODCONTROLLABLEAUDIO_H_
#define MODCONTROLLABLEAUDIO_H_

#include "ModControllable.h"
#include "AudioSample.h"
#include "Delay.h"
#include "lfo.h"
#include "compressor.h"
#include "MidiKnobArray.h"
#include "ParamDescriptor.h"

#define STUTTERER_STATUS_OFF 0
#define STUTTERER_STATUS_RECORDING 1
#define STUTTERER_STATUS_PLAYING 2

struct Stutterer {
	DelayBuffer buffer;
	uint8_t status;
	uint8_t sync;
	int32_t sizeLeftUntilRecordFinished;
};

class Knob;
class MIDIDevice;
class ModelStackWithTimelineCounter;
class ParamManager;

class ModControllableAudio : public ModControllable {
public:
	ModControllableAudio();
	virtual ~ModControllableAudio();
	virtual void cloneFrom(ModControllableAudio* other);

	void processStutter(StereoSample* buffer, int numSamples, ParamManager* paramManager);
	void processReverbSendAndVolume(StereoSample* buffer, int numSamples, int32_t* reverbBuffer, int32_t postFXVolume,
	                                int32_t postReverbVolume, int32_t reverbSendAmount, int32_t pan = 0,
	                                bool doAmplitudeIncrement = false, int32_t amplitudeIncrement = 0);
	void writeAttributesToFile();
	void writeTagsToFile();
	int readTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos,
	                    Song* song);
	void processSRRAndBitcrushing(StereoSample* buffer, int numSamples, int32_t* postFXVolume,
	                              ParamManager* paramManager);
	static void writeParamAttributesToFile(ParamManager* paramManager, bool writeAutomation,
	                                       int32_t* valuesForOverride = NULL);
	static void writeParamTagsToFile(ParamManager* paramManager, bool writeAutomation,
	                                 int32_t* valuesForOverride = NULL);
	static bool readParamTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager,
	                                 int32_t readAutomationUpToPos);
	static void initParams(ParamManager* paramManager);
	void wontBeRenderedForAWhile();
	void endStutter(ParamManagerForTimeline* paramManager);
	virtual bool setModFXType(int newType);
	bool offerReceivedCCToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value,
	                                    ModelStackWithTimelineCounter* modelStack, int noteRowIndex = -1);
	bool offerReceivedPitchBendToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2,
	                                           ModelStackWithTimelineCounter* modelStack, int noteRowIndex = -1);
	virtual bool learnKnob(MIDIDevice* fromDevice, ParamDescriptor paramDescriptor, uint8_t whichKnob,
	                       uint8_t modKnobMode, uint8_t midiChannel, Song* song);
	bool unlearnKnobs(ParamDescriptor paramDescriptor, Song* song);
	virtual void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Song* song) {} // Song may be NULL
	virtual char const* paramToString(uint8_t param);
	virtual int stringToParam(char const* string);
	bool isBitcrushingEnabled(ParamManager* paramManager);
	bool isSRREnabled(ParamManager* paramManager);
	bool hasBassAdjusted(ParamManager* paramManager);
	bool hasTrebleAdjusted(ParamManager* paramManager);
	ModelStackWithAutoParam* getParamFromMIDIKnob(MIDIKnob* knob, ModelStackWithThreeMainThings* modelStack);
	int buttonAction(int x, int y, bool on, ModelStackWithThreeMainThings* modelStack);
	ModelStackWithAutoParam* getParamFromModEncoder(int whichModEncoder, ModelStackWithThreeMainThings* modelStack,
	                                                bool allowCreation);

	// Phaser
	StereoSample phaserMemory;
	StereoSample allpassMemory[PHASER_NUM_ALLPASS_FILTERS];

	// EQ
	int32_t bassFreq; // These two should eventually not be variables like this
	int32_t trebleFreq;

	int32_t withoutTrebleL;
	int32_t bassOnlyL;
	int32_t withoutTrebleR;
	int32_t bassOnlyR;

	// Delay
	Delay delay;

	bool sampleRateReductionOnLastTime;
	uint8_t clippingAmount; // Song probably doesn't currently use this?
	uint8_t lpfMode;

	// Mod FX
	uint8_t modFXType;
	StereoSample* modFXBuffer;
	uint16_t modFXBufferWriteIndex;
	LFO modFXLFO;

	Stutterer stutterer;

	uint32_t lowSampleRatePos;
	uint32_t highSampleRatePos;
	StereoSample lastSample;
	StereoSample grabbedSample;
	StereoSample lastGrabbedSample;

	Compressor compressor; // Song doesn't use this, despite extending this class

	MidiKnobArray midiKnobArray;

protected:
	void processFX(StereoSample* buffer, int numSamples, int modFXType, int32_t modFXRate, int32_t modFXDepth,
	               DelayWorkingState* delayWorkingState, int32_t* postFXVolume, ParamManager* paramManager,
	               int analogDelaySaturationAmount);
	int32_t getStutterRate(ParamManager* paramManager);
	void beginStutter(ParamManagerForTimeline* paramManager);
	void switchDelayPingPong();
	void switchDelayAnalog();
	void switchLPFMode();
	void clearModFXMemory();

private:
	void initializeSecondaryDelayBuffer(int32_t newNativeRate, bool makeNativeRatePreciseRelativeToOtherBuffer);
	void doEQ(bool doBass, bool doTreble, int32_t* inputL, int32_t* inputR, int32_t bassAmount, int32_t trebleAmount);
	ModelStackWithThreeMainThings* addNoteRowIndexAndStuff(ModelStackWithTimelineCounter* modelStack, int noteRowIndex);
};

#endif /* MODCONTROLLABLEAUDIO_H_ */
