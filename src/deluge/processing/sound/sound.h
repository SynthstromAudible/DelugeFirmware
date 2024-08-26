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
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/sample/sample_recorder.h"
#include "modulation/arpeggiator.h"
#include "modulation/knob.h"
#include "modulation/lfo.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patcher.h"
#include "modulation/sidechain/sidechain.h"
#include "processing/source.h"
#include "util/misc.h"

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
	int32_t p; // PARAM_LPF_OFF means none
	int32_t currentValue;
};

#define NUM_MOD_SOURCE_SELECTION_BUTTONS 2

/*
 * Sound can be either an Instrument or a Drum, in the form of SoundInstrument or SoundDrum respectively.
 * These classes are implemented using “multiple inheritance”, which is sacrilegious to many C++ programmers.
 * I (Rohan) consider it to be a more or less appropriate solution in this case and a few others in the Deluge codebase
 * where it’s used. It’s a little while though since I’ve sat and thought about what the alternatives could be and
 * whether anything else would be appropriate.
 *
 * Anyway, Sound (which may be named a bit too broadly) basically means a synth or sample, or any combination of the
 * two. And, to reiterate the above, it can exist as a “synth” as the melodic Output of one entire Clip(s), or as just a
 * Drum - one of the many items in a Kit, normally associated with a row of notes.
 */

class Sound : public ModControllableAudio {
public:
	Sound();

	Patcher patcher;

	ParamLPF paramLPF;

	Source sources[kNumSources];

	// This is for the *global* params only, and begins with FIRST_GLOBAL_PARAM, so subtract that from your p value
	// before accessing this array!
	int32_t paramFinalValues[deluge::modulation::params::kNumParams - deluge::modulation::params::FIRST_GLOBAL];
	int32_t globalSourceValues[util::to_underlying(kFirstLocalSource)];

	uint32_t sourcesChanged; // Applies from first source up to FIRST_UNCHANGEABLE_SOURCE

	LFO globalLFO1;
	LFO globalLFO3;
	LFOConfig lfoConfig[LFO_COUNT];

	bool invertReversed; // Used by the arpeggiator to invert the reverse flag just for the current voice

	// December 3, 2024
	// @todo
	// Commit 6534979986d465a0acf01018e066e1d7ca1d170e
	// From PR #2004 (LFO: cleanups in preparation for LFO2 sync support)
	// (https://github.com/SynthstromAudible/DelugeFirmware/pull/2004)
	// Removed four uint8_t lfo variables from this section and replaced them with the LFOConfig
	// definition above. This unknowingly introduced a "release" bug which changed the sound of the
	// Deluge synth engine compared to 1.1 as reported in issue #2660:
	// (https://github.com/SynthstromAudible/DelugeFirmware/issues/2660)
	// As a solution modKnobs is aligned to 8 bytes. I don't know why this fixes the problem but it seems to work even
	// with other changes to the class. We think the issue relates to the use of "offsetof" in the
	// param and patcher system (related to the paramFinalValues / globalSourceValues definitions above)

	alignas(8) ModKnob modKnobs[kNumModButtons][kNumPhysicalModKnobs];

	int32_t sideChainSendLevel;

	PolyphonyMode polyphonic;
	uint8_t maxVoiceCount{8};

	int16_t transpose;

	uint8_t numUnison;
	int8_t unisonDetune;
	uint8_t unisonStereoSpread;

	// For sending MIDI notes for SoundDrums
	uint8_t outputMidiChannel{MIDI_CHANNEL_NONE};
	uint8_t outputMidiNoteForDrum{MIDI_NOTE_NONE};

	int16_t modulatorTranspose[kNumModulators];
	int8_t modulatorCents[kNumModulators];

	PhaseIncrementFineTuner modulatorTransposers[kNumModulators];

	PhaseIncrementFineTuner unisonDetuners[kMaxNumVoicesUnison];
	int32_t unisonPan[kMaxNumVoicesUnison];

	SynthMode synthMode;
	bool modulator1ToModulator0;

	int32_t volumeNeutralValueForUnison;

	int32_t lastNoteCode;

	// int32_t lastMidiNoteOffSent;

	bool oscillatorSync;

	VoicePriority voicePriority;

	bool skippingRendering;

	uint8_t whichExpressionSourcesChangedAtSynthLevel;

	// I really didn't want to store these here, since they're stored in the ParamManager, but.... complications! Always
	// 0 for Drums - that was part of the problem - a Drum's main ParamManager's expression data has been sent to the
	// "polyphonic" bit, and we don't want it to get referred to twice. These get manually refreshed in setActiveClip().
	int32_t monophonicExpressionValues[kNumExpressionDimensions];

	uint32_t oscRetriggerPhase[kNumSources]; // 4294967295 means "off"
	uint32_t modulatorRetriggerPhase[kNumModulators];

	uint32_t timeStartedSkippingRenderingModFX;
	uint32_t timeStartedSkippingRenderingLFO;
	uint32_t timeStartedSkippingRenderingArp;
	uint32_t startSkippingRenderingAtTime; // Valid when not 0. Allows a wait-time before render skipping starts, for if
	                                       // mod fx are on

	virtual ArpeggiatorSettings* getArpSettings(InstrumentClip* clip = NULL) = 0;
	virtual void setSkippingRendering(bool newSkipping);

	ModFXType getModFXType();
	bool setModFXType(ModFXType newType) final;

	void patchedParamPresetValueChanged(uint8_t p, ModelStackWithSoundFlags* modelStack, int32_t oldValue,
	                                    int32_t newValue);
	void render(ModelStackWithThreeMainThings* modelStack, StereoSample* outputBuffer, int32_t numSamples,
	            int32_t* reverbBuffer, int32_t sideChainHitPending, int32_t reverbAmountAdjust = 134217728,
	            bool shouldLimitDelayFeedback = false, int32_t pitchAdjust = kMaxSampleValue,
	            SampleRecorder* recorder = nullptr);
	void unassignAllVoices();

	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Song* song) final; // Song may be NULL
	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(ModelStackWithThreeMainThings* modelStack);
	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(ParamManager* paramManager);
	void ensureParamPresetValueWithoutKnobIsZero(ModelStackWithAutoParam* modelStack);
	void ensureParamPresetValueWithoutKnobIsZeroWithMinimalDetails(ParamManager* paramManager, int32_t p);

	PatchCableAcceptance maySourcePatchToParam(PatchSource s, uint8_t p, ParamManager* paramManager);

	void resyncGlobalLFOs();

	int8_t getKnobPos(uint8_t p, ParamManagerForTimeline* paramManager, uint32_t timePos, TimelineCounter* counter);
	int32_t getKnobPosBig(int32_t p, ParamManagerForTimeline* paramManager, uint32_t timePos, TimelineCounter* counter);
	bool learnKnob(MIDIDevice* fromDevice, ParamDescriptor paramDescriptor, uint8_t whichKnob, uint8_t modKnobMode,
	               uint8_t midiChannel, Song* song) final;

	bool hasFilters();

	void sampleZoneChanged(MarkerType markerType, int32_t s, ModelStackWithSoundFlags* modelStack);
	void setNumUnison(int32_t newNum, ModelStackWithSoundFlags* modelStack);
	void setUnisonDetune(int32_t newAmount, ModelStackWithSoundFlags* modelStack);
	void setUnisonStereoSpread(int32_t newAmount);
	void setModulatorTranspose(int32_t m, int32_t value, ModelStackWithSoundFlags* modelStack);
	void setModulatorCents(int32_t m, int32_t value, ModelStackWithSoundFlags* modelStack);
	Error readFromFile(Deserializer& reader, ModelStackWithModControllable* modelStack, int32_t readAutomationUpToPos,
	                   ArpeggiatorSettings* arpSettings);
	void writeToFile(Serializer& writer, bool savingSong, ParamManager* paramManager, ArpeggiatorSettings* arpSettings,
	                 const char* pathAttribute = NULL);
	bool allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop = false);

	void voiceUnassigned(ModelStackWithVoice* modelStack);
	bool isSourceActiveCurrently(int32_t s, ParamManagerForTimeline* paramManager);
	bool isSourceActiveEverDisregardingMissingSample(int32_t s, ParamManager* paramManager);
	bool isSourceActiveEver(int32_t s, ParamManager* paramManager);
	bool isNoiseActiveEver(ParamManagerForTimeline* paramManager);
	void noteOn(ModelStackWithThreeMainThings* modelStack, ArpeggiatorBase* arpeggiator, int32_t noteCode,
	            int16_t const* mpeValues, uint32_t sampleSyncLength = 0, int32_t ticksLate = 0,
	            uint32_t samplesLate = 0, int32_t velocity = 64, int32_t fromMIDIChannel = 16);
	void noteOff(ModelStackWithThreeMainThings* modelStack, ArpeggiatorBase* arpeggiator, int32_t noteCode);
	void allNotesOff(ModelStackWithThreeMainThings* modelStack, ArpeggiatorBase* arpeggiator);

	void noteOffPostArpeggiator(ModelStackWithSoundFlags* modelStack, int32_t noteCode = -32768);
	void noteOnPostArpeggiator(ModelStackWithSoundFlags* modelStack, int32_t newNoteCodeBeforeArpeggiation,
	                           int32_t newNoteCodeAfterArpeggiation, int32_t velocity, int16_t const* mpeValues,
	                           uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate,
	                           int32_t fromMIDIChannel = 16);
	void polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t expressionDimension,
	                                              int32_t channelOrNoteNumber,
	                                              MIDICharacteristic whichCharacteristic) override;

	int16_t getMaxOscTranspose(InstrumentClip* clip);
	int16_t getMinOscTranspose();
	void setSynthMode(SynthMode value, Song* song);
	inline SynthMode getSynthMode() { return synthMode; }
	bool anyNoteIsOn();
	virtual bool isDrum() { return false; }
	void setupAsSample(ParamManagerForTimeline* paramManager);
	void recalculateAllVoicePhaseIncrements(ModelStackWithSoundFlags* modelStack);
	Error loadAllAudioFiles(bool mayActuallyReadFiles);
	bool envelopeHasSustainCurrently(int32_t e, ParamManagerForTimeline* paramManager);
	bool envelopeHasSustainEver(int32_t e, ParamManagerForTimeline* paramManager);
	bool renderingOscillatorSyncCurrently(ParamManagerForTimeline* paramManager);
	bool renderingOscillatorSyncEver(ParamManager* paramManager);
	bool hasAnyVoices(bool resetTimeEntered);
	void setupAsBlankSynth(ParamManager* paramManager, bool is_dx = false);
	void setupAsDefaultSynth(ParamManager* paramManager);
	void modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) final;
	bool modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack) final;
	static void writeParamsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation);
	static void readParamsFromFile(Deserializer& reader, ParamManagerForTimeline* paramManager,
	                               int32_t readAutomationUpToPos);
	static bool readParamTagFromFile(Deserializer& reader, char const* tagName, ParamManagerForTimeline* paramManager,
	                                 int32_t readAutomationUpToPos);
	static void initParams(ParamManager* paramManager);
	static Error createParamManagerForLoading(ParamManagerForTimeline* paramManager);
	int32_t hasAnyTimeStretchSyncing(ParamManagerForTimeline* paramManager, bool getSampleLength = false,
	                                 int32_t note = 0);
	int32_t hasCutOrLoopModeSamples(ParamManagerForTimeline* paramManager, int32_t note, bool* anyLooping = NULL);
	bool hasCutModeSamples(ParamManagerForTimeline* paramManager);
	bool allowsVeryLateNoteStart(InstrumentClip* clip, ParamManagerForTimeline* paramManager);
	void fastReleaseAllVoices(ModelStackWithSoundFlags* modelStack);
	void recalculatePatchingToParam(uint8_t p, ParamManagerForTimeline* paramManager);
	void doneReadingFromFile();
	virtual void setupPatchingForAllParamManagers(Song* song) {}
	void compensateVolumeForResonance(ModelStackWithThreeMainThings* modelStack);
	// void channelAftertouchReceivedFromInputMIDIChannel(int32_t newValue);
	ModelStackWithAutoParam* getParamFromModEncoder(int32_t whichModEncoder, ModelStackWithThreeMainThings* modelStack,
	                                                bool allowCreation = true) final;
	void reassessRenderSkippingStatus(ModelStackWithSoundFlags* modelStack, bool shouldJustCutModFX = false);
	void getThingWithMostReverb(Sound** soundWithMostReverb, ParamManager** paramManagerWithMostReverb,
	                            GlobalEffectableForClip** globalEffectableWithMostReverb,
	                            int32_t* highestReverbAmountFound, ParamManagerForTimeline* paramManager);
	virtual bool readTagFromFile(Deserializer& reader, char const* tagName) = 0;
	void detachSourcesFromAudioFiles();
	void confirmNumVoices(char const* error);

	inline int32_t getSmoothedPatchedParamValue(int32_t p,
	                                            ParamManager* paramManager) { // Yup, inlining this helped a tiny bit.
		if (paramLPF.p == p) {
			return paramLPF.currentValue;
		}
		else {
			return paramManager->getPatchedParamSet()->getValue(p);
		}
	}

	void notifyValueChangeViaLPF(int32_t p, bool shouldDoParamLPF, ModelStackWithThreeMainThings const* modelStack,
	                             int32_t oldValue, int32_t newValue, bool fromAutomation);
	void deleteMultiRange(int32_t s, int32_t r);
	void prepareForHibernation();
	void process_postarp_notes(ModelStackWithSoundFlags* modelStackWithSoundFlags, ArpeggiatorSettings* arpSettings,
	                           ArpReturnInstruction instruction);
	void wontBeRenderedForAWhile();
	ModelStackWithAutoParam* getParamFromMIDIKnob(MIDIKnob* knob, ModelStackWithThreeMainThings* modelStack) final;
	virtual ArpeggiatorBase* getArp() = 0;
	void possiblySetupDefaultExpressionPatching(ParamManager* paramManager);

	inline void saturate(int32_t* data, uint32_t* workingValue) {
		// Clipping
		if (clippingAmount) {
			int32_t shiftAmount = (clippingAmount >= 2) ? (clippingAmount - 2) : 0;
			//*data = getTanHUnknown(*data, 5 + clippingAmount) << (shiftAmount);
			*data = getTanHAntialiased(*data, workingValue, 5 + clippingAmount) << (shiftAmount);
		}
	}
	int32_t numVoicesAssigned;
	uint32_t getSyncedLFOPhaseIncrement(const LFOConfig& config);

private:
	uint32_t getGlobalLFOPhaseIncrement(LFO_ID lfoId, deluge::modulation::params::Global param);
	void recalculateModulatorTransposer(uint8_t m, ModelStackWithSoundFlags* modelStack);
	void setupUnisonDetuners(ModelStackWithSoundFlags* modelStack);
	void setupUnisonStereoSpread();
	void calculateEffectiveVolume();
	void ensureKnobReferencesCorrectVolume(Knob* knob);
	Error readTagFromFileOrError(Deserializer& reader, char const* tagName, ParamManagerForTimeline* paramManager,
	                             int32_t readAutomationUpToPos, ArpeggiatorSettings* arpSettings, Song* song);

	void writeSourceToFile(Serializer& writer, int32_t s, char const* tagName);
	Error readSourceFromFile(Deserializer& reader, int32_t s, ParamManagerForTimeline* paramManager,
	                         int32_t readAutomationUpToPos);
	void stopSkippingRendering(ArpeggiatorSettings* arpSettings);
	void startSkippingRendering(ModelStackWithSoundFlags* modelStack);
	void getArpBackInTimeAfterSkippingRendering(ArpeggiatorSettings* arpSettings);
	void doParamLPF(int32_t numSamples, ModelStackWithSoundFlags* modelStack);
	void stopParamLPF(ModelStackWithSoundFlags* modelStack);
	bool renderingVoicesInStereo(ModelStackWithSoundFlags* modelStack);
	void setupDefaultExpressionPatching(ParamManager* paramManager);
	void pushSwitchActionOnEncoderForParam(int32_t p, bool on, ModelStackWithThreeMainThings* modelStack);
	ModelStackWithAutoParam* getParamFromModEncoderDeeper(int32_t whichModEncoder,
	                                                      ModelStackWithThreeMainThings* modelStack,
	                                                      bool allowCreation = true);
};
