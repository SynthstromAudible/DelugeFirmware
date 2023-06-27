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

#ifndef SOUND_H
#define SOUND_H

#include <patcher.h>
#include "definitions.h"
#include "compressor.h"
#include "lfo.h"
#include "source.h"
#include "ModControllableAudio.h"
#include "Arpeggiator.h"
#include "Knob.h"
#include "ParamSet.h"
#include "ParamManager.h"

struct CableGroup;
class StereoSample;
class Voice;
class Kit;
class ParamManagerForTimeline;
class TimelineCounter;
class Clip;
class GlobalEffectableForClip;
class ModelStackWithThreeMainThings;
class ModelStackWithSoundFlags;
class ModelStackWithVoice;
class ModelStackWithModControllable;

#define PARAM_LPF_OFF (-1)

struct ParamLPF {
	int p; // PARAM_LPF_OFF means none
	int32_t currentValue;
};

#define NUM_MOD_SOURCE_SELECTION_BUTTONS 2

/*
 * Sound can be either an Instrument or a Drum, in the form of SoundInstrument or SoundDrum respectively.
 * These classes are implemented using “multiple inheritance”, which is sacrilegious to many C++ programmers.
 * I (Rohan) consider it to be a more or less appropriate solution in this case and a few others in the Deluge codebase where it’s used.
 * It’s a little while though since I’ve sat and thought about what the alternatives could be and whether anything else would be appropriate.
 *
 * Anyway, Sound (which may be named a bit too broadly) basically means a synth or sample, or any combination of the two.
 * And, to reiterate the above, it can exist as a “synth” as the melodic Output of one entire Clip(s),
 * or as just a Drum - one of the many items in a Kit, normally associated with a row of notes.
 */

class Sound : public ModControllableAudio {
public:
	Sound();

	Patcher patcher;

	ParamLPF paramLPF;

	Source sources[NUM_SOURCES];

	int32_t paramFinalValues
	    [NUM_PARAMS
	     - FIRST_GLOBAL_PARAM]; // This is for the *global* params only, and begins with FIRST_GLOBAL_PARAM, so subtract that from your p value before accessing this array!
	int32_t globalSourceValues[FIRST_LOCAL_SOURCE];

	uint32_t sourcesChanged; // Applies from first source up to FIRST_UNCHANGEABLE_SOURCE

	LFO globalLFO;
	uint8_t lfoGlobalWaveType;
	uint8_t lfoLocalWaveType;
	SyncType lfoGlobalSyncType;
	SyncLevel lfoGlobalSyncLevel;

	ModKnob modKnobs[NUM_MOD_BUTTONS][NUM_PHYSICAL_MOD_KNOBS];

	int32_t sideChainSendLevel;

	uint8_t polyphonic;

	int16_t transpose;

	uint8_t numUnison;

	int8_t unisonDetune;

	int16_t modulatorTranspose[numModulators];
	int8_t modulatorCents[numModulators];

	PhaseIncrementFineTuner modulatorTransposers[numModulators];

	PhaseIncrementFineTuner unisonDetuners[maxNumUnison];

	uint8_t synthMode;
	bool modulator1ToModulator0;

	int32_t volumeNeutralValueForUnison;

	int lastNoteCode;

	bool oscillatorSync;

	uint8_t voicePriority;

	bool skippingRendering;

	uint8_t whichExpressionSourcesChangedAtSynthLevel;

	// I really didn't want to store these here, since they're stored in the ParamManager, but.... complications! Always 0
	// for Drums - that was part of the problem - a Drum's main ParamManager's expression data has been sent to the
	// "polyphonic" bit, and we don't want it to get referred to twice. These get manually refreshed in setActiveClip().
	int32_t monophonicExpressionValues[NUM_EXPRESSION_DIMENSIONS];

	uint32_t oscRetriggerPhase[NUM_SOURCES]; // 4294967295 means "off"
	uint32_t modulatorRetriggerPhase[numModulators];

	int32_t postReverbVolumeLastTime;

	uint32_t numSamplesSkippedRenderingForGlobalLFO;
	uint32_t timeStartedSkippingRenderingModFX;
	uint32_t timeStartedSkippingRenderingLFO;
	uint32_t timeStartedSkippingRenderingArp;
	uint32_t
	    startSkippingRenderingAtTime; // Valid when not 0. Allows a wait-time before render skipping starts, for if mod fx are on

	virtual ArpeggiatorSettings* getArpSettings(InstrumentClip* clip = NULL) = 0;
	virtual void setSkippingRendering(bool newSkipping);

	bool setModFXType(int newType) final;

	void patchedParamPresetValueChanged(uint8_t p, ModelStackWithSoundFlags* modelStack, int32_t oldValue,
	                                    int32_t newValue);
	void render(ModelStackWithThreeMainThings* modelStack, StereoSample* outputBuffer, int numSamples,
	            int32_t* reverbBuffer, int32_t sideChainHitPending, int32_t reverbAmountAdjust = 134217728,
	            bool shouldLimitDelayFeedback = false, int32_t pitchAdjust = 16777216);
	void unassignAllVoices();

	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Song* song) final; // Song may be NULL
	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(ModelStackWithThreeMainThings* modelStack);
	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(ParamManager* paramManager);
	void ensureParamPresetValueWithoutKnobIsZero(ModelStackWithAutoParam* modelStack);
	void ensureParamPresetValueWithoutKnobIsZeroWithMinimalDetails(ParamManager* paramManager, int p);

	uint8_t maySourcePatchToParam(uint8_t s, uint8_t p, ParamManager* paramManager);

	void setLFOGlobalSyncType(SyncType newType);
	void setLFOGlobalSyncLevel(SyncLevel newLevel);
	void resyncGlobalLFO();
	void setLFOGlobalWave(uint8_t newWave);

	int8_t getKnobPos(uint8_t p, ParamManagerForTimeline* paramManager, uint32_t timePos, TimelineCounter* counter);
	int32_t getKnobPosBig(int p, ParamManagerForTimeline* paramManager, uint32_t timePos, TimelineCounter* counter);
	bool learnKnob(MIDIDevice* fromDevice, ParamDescriptor paramDescriptor, uint8_t whichKnob, uint8_t modKnobMode,
	               uint8_t midiChannel, Song* song) final;

	bool hasFilters();

	void sampleZoneChanged(int markerType, int s, ModelStackWithSoundFlags* modelStack);
	void setNumUnison(int newNum, ModelStackWithSoundFlags* modelStack);
	void setUnisonDetune(int newAmount, ModelStackWithSoundFlags* modelStack);
	void setModulatorTranspose(int m, int value, ModelStackWithSoundFlags* modelStack);
	void setModulatorCents(int m, int value, ModelStackWithSoundFlags* modelStack);
	int readFromFile(ModelStackWithModControllable* modelStack, int32_t readAutomationUpToPos,
	                 ArpeggiatorSettings* arpSettings);
	void writeToFile(bool savingSong, ParamManager* paramManager, ArpeggiatorSettings* arpSettings);
	bool allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop = false);

	void voiceUnassigned(ModelStackWithVoice* modelStack);
	bool isSourceActiveCurrently(int s, ParamManagerForTimeline* paramManager);
	bool isSourceActiveEverDisregardingMissingSample(int s, ParamManager* paramManager);
	bool isSourceActiveEver(int s, ParamManager* paramManager);
	bool isNoiseActiveEver(ParamManagerForTimeline* paramManager);
	void noteOn(ModelStackWithThreeMainThings* modelStack, ArpeggiatorBase* arpeggiator, int noteCode,
	            int16_t const* mpeValues, uint32_t sampleSyncLength = 0, int32_t ticksLate = 0,
	            uint32_t samplesLate = 0, int velocity = 64, int fromMIDIChannel = 16);
	void allNotesOff(ModelStackWithThreeMainThings* modelStack, ArpeggiatorBase* arpeggiator);

	void noteOffPostArpeggiator(ModelStackWithSoundFlags* modelStack, int noteCode = -32768);
	void noteOnPostArpeggiator(ModelStackWithSoundFlags* modelStack, int newNoteCodeBeforeArpeggiation,
	                           int newNoteCodeAfterArpeggiation, int velocity, int16_t const* mpeValues,
	                           uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate,
	                           int fromMIDIChannel = 16);

	int16_t getMaxOscTranspose(InstrumentClip* clip);
	int16_t getMinOscTranspose();
	void setSynthMode(uint8_t value, Song* song);
	inline uint8_t getSynthMode() { return synthMode; }
	bool anyNoteIsOn();
	virtual bool isDrum() { return false; }
	void setupAsSample(ParamManagerForTimeline* paramManager);
	void recalculateAllVoicePhaseIncrements(ModelStackWithSoundFlags* modelStack);
	int loadAllAudioFiles(bool mayActuallyReadFiles);
	bool envelopeHasSustainCurrently(int e, ParamManagerForTimeline* paramManager);
	bool envelopeHasSustainEver(int e, ParamManagerForTimeline* paramManager);
	bool renderingOscillatorSyncCurrently(ParamManagerForTimeline* paramManager);
	bool renderingOscillatorSyncEver(ParamManager* paramManager);
	bool hasAnyVoices();
	void setupAsBlankSynth(ParamManager* paramManager);
	void setupAsDefaultSynth(ParamManager* paramManager);
	void modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) final;
	bool modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack) final;
	static void writeParamsToFile(ParamManager* paramManager, bool writeAutomation);
	static void readParamsFromFile(ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos);
	static bool readParamTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager,
	                                 int32_t readAutomationUpToPos);
	static void initParams(ParamManager* paramManager);
	static int createParamManagerForLoading(ParamManagerForTimeline* paramManager);
	int32_t hasAnyTimeStretchSyncing(ParamManagerForTimeline* paramManager, bool getSampleLength = false, int note = 0);
	int32_t hasCutOrLoopModeSamples(ParamManagerForTimeline* paramManager, int note, bool* anyLooping = NULL);
	bool hasCutModeSamples(ParamManagerForTimeline* paramManager);
	bool allowsVeryLateNoteStart(InstrumentClip* clip, ParamManagerForTimeline* paramManager);
	void fastReleaseAllVoices(ModelStackWithSoundFlags* modelStack);
	void recalculatePatchingToParam(uint8_t p, ParamManagerForTimeline* paramManager);
	void doneReadingFromFile();
	virtual void setupPatchingForAllParamManagers(Song* song) {}
	void compensateVolumeForResonance(ModelStackWithThreeMainThings* modelStack);
	//void channelAftertouchReceivedFromInputMIDIChannel(int newValue);
	ModelStackWithAutoParam* getParamFromModEncoder(int whichModEncoder, ModelStackWithThreeMainThings* modelStack,
	                                                bool allowCreation = true) final;
	void reassessRenderSkippingStatus(ModelStackWithSoundFlags* modelStack, bool shouldJustCutModFX = false);
	void getThingWithMostReverb(Sound** soundWithMostReverb, ParamManager** paramManagerWithMostReverb,
	                            GlobalEffectableForClip** globalEffectableWithMostReverb,
	                            int32_t* highestReverbAmountFound, ParamManagerForTimeline* paramManager);
	virtual bool readTagFromFile(char const* tagName) = 0;
	void detachSourcesFromAudioFiles();
	void confirmNumVoices(char const* error);

	inline int32_t getSmoothedPatchedParamValue(int p,
	                                            ParamManager* paramManager) { // Yup, inlining this helped a tiny bit.
		if (paramLPF.p == p) {
			return paramLPF.currentValue;
		}
		else {
			return paramManager->getPatchedParamSet()->getValue(p);
		}
	}

	void notifyValueChangeViaLPF(int p, bool shouldDoParamLPF, ModelStackWithThreeMainThings const* modelStack,
	                             int32_t oldValue, int32_t newValue, bool fromAutomation);
	void deleteMultiRange(int s, int r);
	void prepareForHibernation();
	void wontBeRenderedForAWhile();
	char const* paramToString(uint8_t param) final;
	int stringToParam(char const* string) final;
	ModelStackWithAutoParam* getParamFromMIDIKnob(MIDIKnob* knob, ModelStackWithThreeMainThings* modelStack) final;
	virtual ArpeggiatorBase* getArp() = 0;
	void possiblySetupDefaultExpressionPatching(ParamManager* paramManager);

	inline void saturate(int32_t* data, uint32_t* workingValue) {
		// Clipping
		if (clippingAmount) {
			int shiftAmount = (clippingAmount >= 2) ? (clippingAmount - 2) : 0;
			//*data = getTanHUnknown(*data, 5 + clippingAmount) << (shiftAmount);
			*data = getTanHAntialiased(*data, workingValue, 5 + clippingAmount) << (shiftAmount);
		}
	}

	int numVoicesAssigned;

private:
	uint32_t getGlobalLFOPhaseIncrement();
	void recalculateModulatorTransposer(uint8_t m, ModelStackWithSoundFlags* modelStack);
	void setupUnisonDetuners(ModelStackWithSoundFlags* modelStack);
	void calculateEffectiveVolume();
	void ensureKnobReferencesCorrectVolume(Knob* knob);
	int readTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos,
	                    ArpeggiatorSettings* arpSettings, Song* song);

	void writeSourceToFile(int s, char const* tagName);
	int readSourceFromFile(int s, ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos);
	void stopSkippingRendering(ArpeggiatorSettings* arpSettings);
	void startSkippingRendering(ModelStackWithSoundFlags* modelStack);
	void getArpBackInTimeAfterSkippingRendering(ArpeggiatorSettings* arpSettings);
	void doParamLPF(int numSamples, ModelStackWithSoundFlags* modelStack);
	void stopParamLPF(ModelStackWithSoundFlags* modelStack);
	bool renderingVoicesInStereo(ModelStackWithSoundFlags* modelStack);
	void setupDefaultExpressionPatching(ParamManager* paramManager);
	void pushSwitchActionOnEncoderForParam(int p, bool on, ModelStackWithThreeMainThings* modelStack);
	ModelStackWithAutoParam* getParamFromModEncoderDeeper(int whichModEncoder,
	                                                      ModelStackWithThreeMainThings* modelStack,
	                                                      bool allowCreation = true);
};

#endif // SOUND_H
