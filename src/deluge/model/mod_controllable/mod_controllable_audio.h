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
#include "dsp/stereo_sample.h"
#include "engines/effects/delay.h"
#include "hid/button.h"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/mod_controllable/mod_controllable.h"
#include "model/snapshot/mod_controllable_snapshot.h"
#include "modulation/knob.h"
#include "modulation/params/param_descriptor.h"
#include "modulation/params/param_set.h"
#include "synth/lfo.h"

#include "engine-api/engine.h"
#include "engines/effects/chain_core.h"
#include "engines/effects/state.h"

class Clip;
class Sound;
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

	void processStutter(std::span<StereoSample> buffer, ParamManager* paramManager);
	void processReverbSendAndVolume(std::span<StereoSample> buffer, int32_t* reverbBuffer, int32_t postFXVolume,
	                                int32_t postReverbVolume, int32_t reverbSendAmount, int32_t pan = 0,
	                                bool doAmplitudeIncrement = false);
	void writeAttributesToFile(Serializer& writer);
	void writeTagsToFile(Serializer& writer);
	/// The learned MIDI knobs' tag, which a file writes between the delay and the other effect tags.
	void writeMidiKnobsToFile(Serializer& writer);
	/// The codec context a file's effect entries are written in: the current song's tick magnitude.
	[[nodiscard]] static deluge::engine::CodecContext effectsCodecContext();
	/// The compressor threshold a file saves: the one the render last applied, which for a sound or a clip chain is its
	/// param's.
	[[nodiscard]] virtual q31_t savedCompressorThreshold() const { return core().compressor().dsp().threshold(); }

	/// @brief This owner's effect settings: a bound sound's are its SoundState's, any other owner's its own.
	[[nodiscard]] const deluge::effects::EffectStates& effects() const {
		return boundEffects_ != nullptr ? *boundEffects_ : ownEffects_;
	}
	/// @brief Make @p next this owner's effect settings, and the render's copy of them, which is replaced with the
	/// render excluded.
	void setEffects(const deluge::effects::EffectStates& next);
	/// @brief Keep this owner's effect settings in @p record (a sound's SoundState), which takes the settings held now;
	/// nullptr keeps them here again.
	void bindEffects(deluge::effects::EffectStates* record);
	/// The render's copy of the effect settings: it reads nothing else of them.
	deluge::snapshot::ModControllableSnapshot modControllableSnapshot_{};
	[[nodiscard]] const deluge::snapshot::ModControllableSnapshot& modControllableSnapshot() const {
		return modControllableSnapshot_;
	}
	/// Copy the settings into the render's copy, with the render excluded.
	void projectModControllableSnapshot();
	/// Store the mod-FX type; setModFXType() does the same and returns true. For the load path and
	/// cloneFrom().
	void assignModFXType(ModFXType value);
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
	/// This owner as a sound, or nullptr for a kit, an audio track or the song master.
	[[nodiscard]] virtual ::Sound* asSound() { return nullptr; }
	bool setModFXType(ModFXType newType);
	/// Clear the mod FX's memory, as a sound cutting its tail does: a comb type's delay line or phaser memories, or the
	/// grain's grains and first pass.
	void clearModFXMemory();
	// One effect setting each, through setEffects.
	void setClippingAmount(uint8_t value);
	void setLpfMode(FilterMode value);
	void setHpfMode(FilterMode value);
	void setFilterRoute(FilterRoute value);
	void setDelayPingPong(bool value);
	void setDelayType(deluge::effects::DelayType value);
	/// Set by a read whose element saves no delay type: the type is worked out once the owner's params are read
	/// (deluge::model::resolvePendingDelayTypes).
	bool delayTypePending = false;
	void setDelaySyncType(SyncType value);
	void setDelaySyncLevel(SyncLevel value);
	void setStutterQuantized(bool value);
	void setStutterReversed(bool value);
	void setStutterPingPong(bool value);
	void setUseSongStutter(bool value);
	// The reverb's sidechain (AudioEngine::reverbSidechain) is the engine's own, not an owner's.
	void setSidechainAttack(int32_t value);
	void setSidechainRelease(int32_t value);
	void setSidechainSyncType(SyncType value);
	void setSidechainSyncLevel(SyncLevel value);
	/// The compressor's knob positions, each Q31 (the menu's 0–127 shifted up by 24).
	void setCompressorAttack(q31_t value);
	void setCompressorRelease(q31_t value);
	void setCompressorThreshold(q31_t value);
	void setCompressorRatio(q31_t value);
	void setCompressorSidechainHpf(q31_t value);
	void setCompressorBlend(q31_t value);
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
	ModelStackWithAutoParam* getParamFromMIDIKnob(MIDIKnob& knob, ModelStackWithThreeMainThings* modelStack) override;

	/// The effect components every owner has, in its chain: a sound's, a clip chain's or the master's.
	[[nodiscard]] virtual deluge::effects::ChainCore& core() = 0;
	[[nodiscard]] virtual const deluge::effects::ChainCore& core() const = 0;

	deluge::fast_vector<MIDIKnob> midi_knobs{};

private:
	deluge::effects::EffectStates ownEffects_{};
	deluge::effects::EffectStates* boundEffects_{nullptr};

protected:
	/// @brief Gives the delay's tapes back to the arena and leaves its margin, as a change to OFF does. Each owner
	/// calls it first in its destructor, while its chain still exists.
	void releaseChainMemory();
	/// @param reverbBuffer The block's reverb send buffer, which the grain adds its feed into from index 0.
	void processFX(std::span<StereoSample> buffer, ModFXType modFXType, int32_t modFXRate, int32_t modFXDepth,
	               const deluge::effects::Delay::State& delayWorkingState, int32_t* postFXVolume,
	               ParamManager* paramManager, bool anySoundComingIn, q31_t reverbSendAmount, int32_t* reverbBuffer);
	void switchDelayPingPong();
	/// One press of the delay's feedback knob: digital, analog, off, digital, by one Edit.
	void cycleDelayType();
	void switchDelaySyncType();
	void switchDelaySyncLevel();
	void switchLPFMode();
	void switchHPFMode();

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

private:
	ModelStackWithThreeMainThings* addNoteRowIndexAndStuff(ModelStackWithTimelineCounter* modelStack,
	                                                       int32_t noteRowIndex);
	void switchHPFModeWithOff();
	void switchLPFModeWithOff();

	void processGrainFX(std::span<StereoSample> buffer, int32_t modFXRate, int32_t modFXDepth, int32_t* postFXVolume,
	                    UnpatchedParamSet* unpatchedParams, bool anySoundComingIn, q31_t verbAmount,
	                    int32_t* reverbBuffer);
};
