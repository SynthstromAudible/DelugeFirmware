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
#include "deluge/dsp/granular/GranularProcessor.h"
#include "dsp/compressor/multiband.h"
#include "dsp/compressor/rms_feedback.h"
#include "dsp/delay/delay.h"
#include "dsp/shaper.h"
#include "dsp/sine_shaper.hpp"
#include "dsp/stereo_sample.h"
#include "hid/button.h"
#include "model/fx/stutterer.h"
#include "model/mod_controllable/ModFXProcessor.h"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/mod_controllable/mod_controllable.h"
#include "modulation/knob.h"
#include "modulation/lfo.h"
#include "modulation/midi/midi_knob_array.h"
#include "modulation/params/param_descriptor.h"
#include "modulation/params/param_set.h"
#include "modulation/sidechain/sidechain.h"

class Clip;
class Knob;
class MIDICable;
class ModelStack;
class ModelStackWithTimelineCounter;
class ParamManager;
class Serializer;
class Serializer;
class Deserializer;

class ModControllableAudio : public ModControllable {
public:
	ModControllableAudio();
	virtual ~ModControllableAudio();
	virtual void cloneFrom(ModControllableAudio* other);

	void processStutter(std::span<StereoSample> buffer, ParamManager* paramManager,
	                    const q31_t* modulatedScatterValues = nullptr);
	void processReverbSendAndVolume(std::span<StereoSample> buffer, int32_t* reverbBuffer, int32_t postFXVolume,
	                                int32_t postReverbVolume, int32_t reverbSendAmount, int32_t pan = 0,
	                                bool doAmplitudeIncrement = false);
	void writeAttributesToFile(Serializer& writer);
	void writeTagsToFile(Serializer& writer);
	virtual Error readTagFromFile(Deserializer& reader, char const* tagName, ParamManagerForTimeline* paramManager,
	                              int32_t readAutomationUpToPos, ArpeggiatorSettings* arpSettings, Song* song);
	void processSRRAndBitcrushing(std::span<StereoSample> buffer, int32_t* postFXVolume, ParamManager* paramManager);
	static void writeParamAttributesToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
	                                       int32_t* valuesForOverride = nullptr);
	static void writeParamTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
	                                 int32_t* valuesForOverride = nullptr);
	static bool readParamTagFromFile(Deserializer& reader, char const* tagName, ParamManagerForTimeline* paramManager,
	                                 int32_t readAutomationUpToPos);
	static void initParams(ParamManager* paramManager);
	virtual void wontBeRenderedForAWhile();
	void beginStutter(ParamManagerForTimeline* paramManager);
	void endStutter(ParamManagerForTimeline* paramManager);
	virtual ModFXType getModFXType() = 0;
	virtual bool setModFXType(ModFXType newType);
	bool offerReceivedCCToLearnedParamsForClip(MIDICable& cable, uint8_t channel, uint8_t ccNumber, uint8_t value,
	                                           ModelStackWithTimelineCounter* modelStack, int32_t noteRowIndex = -1);
	bool offerReceivedCCToLearnedParamsForSong(MIDICable& cable, uint8_t channel, uint8_t ccNumber, uint8_t value,
	                                           ModelStackWithThreeMainThings* modelStackWithThreeMainThings);
	bool offerReceivedPitchBendToLearnedParams(MIDICable& cable, uint8_t channel, uint8_t data1, uint8_t data2,
	                                           ModelStackWithTimelineCounter* modelStack, int32_t noteRowIndex = -1);
	/// Learna knob to a particular parameter.
	///
	/// @param cable The source MIDI cable, or null if learning a mod knob
	virtual bool learnKnob(MIDICable* cable, ParamDescriptor paramDescriptor, uint8_t whichKnob, uint8_t modKnobMode,
	                       uint8_t midiChannel, Song* song);
	bool unlearnKnobs(ParamDescriptor paramDescriptor, Song* song);
	virtual void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Song* song) {} // Song may be NULL
	bool isBitcrushingEnabled(ParamManager* paramManager);
	bool isSRREnabled(ParamManager* paramManager);
	bool hasBassAdjusted(ParamManager* paramManager);
	bool hasTrebleAdjusted(ParamManager* paramManager);
	ModelStackWithAutoParam* getParamFromMIDIKnob(MIDIKnob& knob, ModelStackWithThreeMainThings* modelStack) override;

	// EQ
	int32_t bassFreq{}; // These two should eventually not be variables like this
	int32_t trebleFreq{};

	int32_t withoutTrebleL;
	int32_t bassOnlyL;
	int32_t withoutTrebleR;
	int32_t bassOnlyR;

	// Delay
	Delay delay;
	StutterConfig stutterConfig;

	bool sampleRateReductionOnLastTime;
	uint8_t clippingAmount; // Song probably doesn't currently use this?

	// Table Shaper with X/Y shape control
	deluge::dsp::TableShaper shaperDsp;   // DSP processor with lookup table
	deluge::dsp::TableShaperState shaper; // All shaper state (knob values, smoothing, etc.)

	// Sine shaper parameters and DSP state (struct defined in dsp/sine_shaper.hpp)
	deluge::dsp::SineTableShaperParams sineShaper;

	FilterMode lpfMode;
	FilterMode hpfMode;
	FilterRoute filterRoute;

	// Mod FX
	ModFXType modFXType_;
	ModFXProcessor modfx{};
	RMSFeedbackCompressor compressor;
	deluge::dsp::MultibandCompressor multibandCompressor;
	CompressorMode compressorMode{CompressorMode::SINGLE};

	/// Apply modulated params from UnpatchedParamSet to multiband compressor before rendering
	void applyMultibandCompressorParams(ParamManager* paramManager);
	GranularProcessor* grainFX{nullptr};

	uint32_t lowSampleRatePos{};
	uint32_t highSampleRatePos{};
	StereoSample lastSample;
	StereoSample grabbedSample;
	StereoSample lastGrabbedSample;

	SideChain sidechain; // Song doesn't use this, despite extending this class

	deluge::fast_vector<MIDIKnob> midi_knobs;
	int32_t postReverbVolumeLastTime{};

protected:
	void processFX(std::span<StereoSample> buffer, ModFXType modFXType, int32_t modFXRate, int32_t modFXDepth,
	               const Delay::State& delayWorkingState, int32_t* postFXVolume, ParamManager* paramManager,
	               bool anySoundComingIn, q31_t reverbSendAmount);
	void switchDelayPingPong();
	void switchDelayAnalog();
	void switchDelaySyncType();
	void switchDelaySyncLevel();
	void switchLPFMode();
	void switchHPFMode();
	void clearModFXMemory();

	/// What kind of unpatched parameters this ModControllable uses.
	///
	/// This should be UNPATCHED_GLOBAL for GlobalEffectable and UNPATCHED_SOUND for Sound. If a new ModControllable
	/// subclass is
	deluge::modulation::params::Kind unpatchedParamKind_;

	char const* getFilterTypeDisplayName(FilterType currentFilterType);
	char const* getFilterModeDisplayName(FilterType currentFilterType);
	char const* getLPFModeDisplayName();
	char const* getHPFModeDisplayName();
	char const* getDelayTypeDisplayName();
	char const* getDelayPingPongStatusDisplayName();
	char const* getDelaySyncTypeDisplayName();
	void getDelaySyncLevelDisplayName(char* displayName);
	char const* getSidechainDisplayName();

	void displayFilterSettings(bool on, FilterType currentFilterType);
	void displayDelaySettings(bool on);
	void displaySidechainAndReverbSettings(bool on);
	void displayOtherModKnobSettings(uint8_t whichModButton, bool on);

protected:
	// returns whether it succeeded
	bool enableGrain();
	void disableGrain();

private:
	void doEQ(bool doBass, bool doTreble, int32_t* inputL, int32_t* inputR, int32_t bassAmount, int32_t trebleAmount);
	ModelStackWithThreeMainThings* addNoteRowIndexAndStuff(ModelStackWithTimelineCounter* modelStack,
	                                                       int32_t noteRowIndex);
	void switchHPFModeWithOff();
	void switchLPFModeWithOff();

	void processGrainFX(std::span<StereoSample> buffer, int32_t modFXRate, int32_t modFXDepth, int32_t* postFXVolume,
	                    UnpatchedParamSet* unpatchedParams, bool anySoundComingIn, q31_t verbAmount);
};
