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
#include "dsp/delay/delay.h"
#include "dsp/stereo_sample.h"
#include "hid/button.h"
#include "model/fx/stutterer.h"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/mod_controllable/mod_controllable.h"
#include "model/snapshot/mod_controllable_snapshot.h"
#include "modulation/knob.h"
#include "modulation/params/param_descriptor.h"
#include "modulation/params/param_set.h"
#include "synth/lfo.h"

#include "engine-api/engine.h"
#include "engines/effects/bitcrush.h"
#include "engines/effects/compressor.h"
#include "engines/effects/equalizer.h"
#include "engines/effects/granular.h"
#include "engines/effects/mod_fx.h"
#include "engines/effects/send_and_volume.h"
#include "engines/effects/sidechain.h"
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
	/// @brief An owner whose entry into GRAIN does @p enterGrain to its granular effect: CLEAR for a sound, KEEP for a
	/// clip or master chain.
	explicit ModControllableAudio(deluge::effects::EnterGrain enterGrain);
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
	[[nodiscard]] virtual q31_t savedCompressorThreshold() const { return compressor.dsp().threshold(); }

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
	/// Store the mod-FX type without setModFXType()'s owner-specific setup — for the load path and cloneFrom().
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
	virtual bool setModFXType(ModFXType newType);
	/// Clear the mod FX's memory, as a sound cutting its tail does: a comb type's delay line or phaser memories, or the
	/// grain's grains and first pass.
	void clearModFXMemory();
	// One effect setting each, through setEffects.
	void setClippingAmount(uint8_t value);
	void setLpfMode(FilterMode value);
	void setHpfMode(FilterMode value);
	void setFilterRoute(FilterRoute value);
	void setDelayPingPong(bool value);
	void setDelayAnalog(bool value);
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

	/// The two-band EQ, between the mod FX and the delay.
	deluge::effects::Equalizer equalizer_;

	// Delay
	Delay delay;

	/// Bitcrush and sample-rate reduction, with the conversion memory the latter carries.
	deluge::effects::Bitcrush bitcrush_;

	/// The mod FX but for the grain: its ring is taken and given back where the type changes, in
	/// projectModControllableSnapshot().
	deluge::effects::ModFx modfx;
	deluge::effects::Compressor compressor;
	/// The grain: its State is taken on the first entry into GRAIN and its ring while the type is GRAIN, both in
	/// projectModControllableSnapshot().
	deluge::effects::Granular granular;

	deluge::effects::Sidechain sidechain; // Song doesn't use this, despite extending this class

	deluge::fast_vector<MIDIKnob> midi_knobs{};
	/// The reverb send, post-FX volume and pan, which ramps the post-reverb volume from the last block's.
	deluge::effects::SendAndVolume sendAndVolume_;

private:
	deluge::effects::EffectStates ownEffects_{};
	deluge::effects::EffectStates* boundEffects_{nullptr};

protected:
	/// @param reverbBuffer The block's reverb send buffer, which the grain adds its feed into from index 0.
	void processFX(std::span<StereoSample> buffer, ModFXType modFXType, int32_t modFXRate, int32_t modFXDepth,
	               const Delay::State& delayWorkingState, int32_t* postFXVolume, ParamManager* paramManager,
	               bool anySoundComingIn, q31_t reverbSendAmount, int32_t* reverbBuffer);
	void switchDelayPingPong();
	void switchDelayAnalog();
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
