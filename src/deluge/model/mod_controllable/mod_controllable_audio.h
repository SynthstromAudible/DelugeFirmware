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

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/compressor/compressor.h"
#include "dsp/delay/delay.h"
#include "dsp/stereo_sample.h"
#include "hid/button.h"
#include "model/mod_controllable/mod_controllable.h"
#include "modulation/lfo.h"
#include "modulation/midi/midi_knob_array.h"
#include "modulation/params/param_descriptor.h"

#define STUTTERER_STATUS_OFF 0
#define STUTTERER_STATUS_RECORDING 1
#define STUTTERER_STATUS_PLAYING 2

struct Stutterer {
	DelayBuffer buffer;
	uint8_t status;
	uint8_t sync;
	int32_t sizeLeftUntilRecordFinished;
	int32_t valueBeforeStuttering;
	int32_t lastQuantizedKnobDiff;
};

struct Grain {
	int32_t length;     //in samples 0=OFF
	int32_t startPoint; //starttimepos in samples
	int32_t counter;    //relative pos in samples
	uint16_t pitch;     //1024=1.0
	int32_t volScale;
	int32_t volScaleMax;
	bool rev;        //0=normal, 1 =reverse
	int32_t panVolL; //0 - 1073741823
	int32_t panVolR; //0 - 1073741823
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

	void processStutter(StereoSample* buffer, int32_t numSamples, ParamManager* paramManager);
	void processReverbSendAndVolume(StereoSample* buffer, int32_t numSamples, int32_t* reverbBuffer,
	                                int32_t postFXVolume, int32_t postReverbVolume, int32_t reverbSendAmount,
	                                int32_t pan = 0, bool doAmplitudeIncrement = false, int32_t amplitudeIncrement = 0);
	void writeAttributesToFile();
	void writeTagsToFile();
	int32_t readTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos,
	                        Song* song);
	void processSRRAndBitcrushing(StereoSample* buffer, int32_t numSamples, int32_t* postFXVolume,
	                              ParamManager* paramManager);
	static void writeParamAttributesToFile(ParamManager* paramManager, bool writeAutomation,
	                                       int32_t* valuesForOverride = NULL);
	static void writeParamTagsToFile(ParamManager* paramManager, bool writeAutomation,
	                                 int32_t* valuesForOverride = NULL);
	static bool readParamTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager,
	                                 int32_t readAutomationUpToPos);
	static void initParams(ParamManager* paramManager);
	virtual void wontBeRenderedForAWhile();
	void beginStutter(ParamManagerForTimeline* paramManager);
	void endStutter(ParamManagerForTimeline* paramManager);
	virtual bool setModFXType(ModFXType newType);
	bool offerReceivedCCToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value,
	                                    ModelStackWithTimelineCounter* modelStack, int32_t noteRowIndex = -1);
	void offerReceivedCCToMidiFollow(int32_t ccNumber, int32_t value);
	void sendCCWithoutModelStackForMidiFollowFeedback(bool isAutomation = false);
	void sendCCForMidiFollowFeedback(int32_t ccNumber, int32_t knobPos);
	bool offerReceivedPitchBendToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2,
	                                           ModelStackWithTimelineCounter* modelStack, int32_t noteRowIndex = -1);
	virtual bool learnKnob(MIDIDevice* fromDevice, ParamDescriptor paramDescriptor, uint8_t whichKnob,
	                       uint8_t modKnobMode, uint8_t midiChannel, Song* song);
	bool unlearnKnobs(ParamDescriptor paramDescriptor, Song* song);
	virtual void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Song* song) {} // Song may be NULL
	virtual char const* paramToString(uint8_t param);
	virtual int32_t stringToParam(char const* string);
	bool isBitcrushingEnabled(ParamManager* paramManager);
	bool isSRREnabled(ParamManager* paramManager);
	bool hasBassAdjusted(ParamManager* paramManager);
	bool hasTrebleAdjusted(ParamManager* paramManager);
	ModelStackWithAutoParam* getParamFromMIDIKnob(MIDIKnob* knob, ModelStackWithThreeMainThings* modelStack);
	ActionResult buttonAction(deluge::hid::Button b, bool on, ModelStackWithThreeMainThings* modelStack);
	ModelStackWithAutoParam* getParamFromModEncoder(int32_t whichModEncoder, ModelStackWithThreeMainThings* modelStack,
	                                                bool allowCreation);

	// Phaser
	StereoSample phaserMemory;
	StereoSample allpassMemory[kNumAllpassFiltersPhaser];

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
	FilterMode lpfMode;
	FilterMode hpfMode;
	FilterRoute filterRoute;

	// Mod FX
	ModFXType modFXType;
	StereoSample* modFXBuffer;
	uint16_t modFXBufferWriteIndex;
	LFO modFXLFO;

	//Grain
	int32_t wrapsToShutdown;
	void setWrapsToShutdown();
	StereoSample* modFXGrainBuffer;
	uint32_t modFXGrainBufferWriteIndex;
	int32_t grainSize;
	int32_t grainRate;
	int32_t grainShift;
	Grain grains[8];
	int32_t grainFeedbackVol;
	int32_t grainVol;
	int32_t grainDryVol;
	int8_t grainPitchType;
	bool grainLastTickCountIsZero;
	bool grainInitialized;

	Stutterer stutterer;

	uint32_t lowSampleRatePos;
	uint32_t highSampleRatePos;
	StereoSample lastSample;
	StereoSample grabbedSample;
	StereoSample lastGrabbedSample;

	Compressor compressor; // Song doesn't use this, despite extending this class

	MidiKnobArray midiKnobArray;

private:
	int32_t calculateKnobPosForMidiTakeover(ModelStackWithAutoParam* modelStackWithParam, int32_t modPos, int32_t value,
	                                        MIDIKnob* knob = nullptr, bool midiFollow = false, int32_t xDisplay = 0,
	                                        int32_t yDisplay = 0);

protected:
	void processFX(StereoSample* buffer, int32_t numSamples, ModFXType modFXType, int32_t modFXRate, int32_t modFXDepth,
	               DelayWorkingState* delayWorkingState, int32_t* postFXVolume, ParamManager* paramManager,
	               int32_t analogDelaySaturationAmount);
	int32_t getStutterRate(ParamManager* paramManager);
	void switchDelayPingPong();
	void switchDelayAnalog();
	void switchDelaySyncType();
	void switchDelaySyncLevel();
	void switchLPFMode();
	void switchHPFMode();
	void clearModFXMemory();

private:
	void initializeSecondaryDelayBuffer(int32_t newNativeRate, bool makeNativeRatePreciseRelativeToOtherBuffer);
	void doEQ(bool doBass, bool doTreble, int32_t* inputL, int32_t* inputR, int32_t bassAmount, int32_t trebleAmount);
	ModelStackWithThreeMainThings* addNoteRowIndexAndStuff(ModelStackWithTimelineCounter* modelStack,
	                                                       int32_t noteRowIndex);
};
