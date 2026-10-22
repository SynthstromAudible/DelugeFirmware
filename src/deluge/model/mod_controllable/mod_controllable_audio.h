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
#include "deluge-project/controller_mapping.h"
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
class GlobalEffectable;
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

	void writeAttributesToFile(Serializer& writer);
	/// @brief Writes @p states' effect attributes.
	/// @param writer Destination.
	/// @param states The effect settings to write.
	void writeAttributesToFile(Serializer& writer, const deluge::effects::EffectStates& states);
	/// @brief Writes @p states' effect tags, the learned MIDI knobs among them; @p compressorThreshold, when given, in
	/// place of the state's threshold.
	/// @param writer              Destination.
	/// @param states              The effect settings to write.
	/// @param knobsSong           The song whose table the learned knobs are written from (see writeMidiKnobsToFile).
	/// @param knobsOwner          Whose controls these are, for the learned knobs (see writeMidiKnobsToFile).
	/// @param compressorThreshold Threshold (Q31) to write instead of the state's, if given.
	void writeTagsToFile(Serializer& writer, const deluge::effects::EffectStates& states, Song* knobsSong,
	                     std::optional<deluge::project::ControlsAddress> knobsOwner,
	                     std::optional<q31_t> compressorThreshold = std::nullopt);
	/// @brief Writes the learned MIDI knobs' tag, which a file writes between the delay and the other effect tags: the
	/// knobs @p song's mapping table holds for @p owner, in table order. An owner @p song does not hold (a preset
	/// loaded outside it) has no table, so its own knob list is written.
	/// @param writer Destination.
	/// @param song   The song being saved, or the one a preset is saved from; nullptr when there is none.
	/// @param owner  Whose controls these are, if known.
	void writeMidiKnobsToFile(Serializer& writer, Song* song, std::optional<deluge::project::ControlsAddress> owner);
	/// @brief The codec context a file's effect entries are written in: the current song's tick magnitude, even when
	/// the song being saved is another (the learned knobs take the saved song; this does not).
	/// @return The codec context.
	[[nodiscard]] static deluge::engine::CodecContext effectsCodecContext();
	/// @brief The compressor threshold a sound's file saves: the one the render last applied, its param's.
	/// @return The threshold (Q31).
	[[nodiscard]] q31_t savedCompressorThreshold() const { return core().compressor().dsp().threshold(); }

	/// @brief This owner's effect settings: a bound sound's are its SoundState's, any other owner's its own.
	/// @return The effect settings.
	[[nodiscard]] const deluge::effects::EffectStates& effects() const {
		return boundEffects_ != nullptr ? *boundEffects_ : ownEffects_;
	}
	/// @brief Make @p next this owner's effect settings, and the render's copy of them, which is replaced with the
	/// render excluded.
	/// @param next The new effect settings.
	void setEffects(const deluge::effects::EffectStates& next);
	/// @brief Keep this owner's effect settings in @p record (a sound's SoundState), which takes the settings held now;
	/// nullptr keeps them here again.
	/// @param record The record that holds the settings, or nullptr to hold them in this owner.
	void bindEffects(deluge::effects::EffectStates* record);
	/// @brief The render's copy of the effect settings: it reads nothing else of them.
	deluge::snapshot::ModControllableSnapshot modControllableSnapshot_{};
	/// @brief The render's copy of the effect settings.
	/// @return The render's copy.
	[[nodiscard]] const deluge::snapshot::ModControllableSnapshot& modControllableSnapshot() const {
		return modControllableSnapshot_;
	}
	/// @brief Copy the settings into the render's copy, with the render excluded.
	void projectModControllableSnapshot();
	/// @brief Inside projectModControllableSnapshot's excluded section: the owner's view into @p copy, for a host chain
	/// (its selected mod FX knob); nothing for a sound.
	/// @param copy The render's copy being filled.
	virtual void projectView(deluge::snapshot::ModControllableSnapshot& /*copy*/) {}
	/// @brief Store the mod-FX type; setModFXType() does the same and returns true. For the load path and
	/// cloneFrom().
	/// @param value The mod FX type to store.
	void assignModFXType(ModFXType value);
	virtual Error readTagFromFile(Deserializer& reader, char const* tagName, ParamManagerForTimeline* paramManager,
	                              int32_t readAutomationUpToPos, ArpeggiatorSettings* arpSettings, Song* song);
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
	/// @brief This owner as a sound.
	/// @return The sound, or nullptr for a kit, an audio track or the song master.
	[[nodiscard]] virtual ::Sound* asSound() { return nullptr; }
	/// @brief This owner as a host chain's owner (a kit, an audio track, the song master), or null for a sound.
	/// @return The owner, or null.
	[[nodiscard]] virtual const ::GlobalEffectable* asGlobalEffectable() const { return nullptr; }
	bool setModFXType(ModFXType newType);
	/// @brief Clear the mod FX's memory, as a sound cutting its tail does: a comb type's delay line or phaser memories,
	/// or the grain's grains and first pass.
	void clearModFXMemory();
	/// @name "Effect setters"
	/// One effect setting each, through setEffects.
	/// @{
	void setClippingAmount(uint8_t value);
	void setLpfMode(FilterMode value);
	void setHpfMode(FilterMode value);
	void setFilterRoute(FilterRoute value);
	void setDelayPingPong(bool value);
	void setDelayType(deluge::effects::DelayType value);
	/// @brief Set by a read whose element saves no delay type: the type is worked out once the owner's params are read
	/// (deluge::model::resolvePendingDelayTypes).
	bool delayTypePending = false;
	/// @brief Set by a read whose element held an arpeggiator (a synth track's own, which its record then keeps).
	bool arpRead = false;
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
	// The compressor's knob positions, each Q31 (the menu's 0-127 shifted up by 24).
	void setCompressorAttack(q31_t value);
	void setCompressorRelease(q31_t value);
	void setCompressorThreshold(q31_t value);
	void setCompressorRatio(q31_t value);
	void setCompressorSidechainHpf(q31_t value);
	void setCompressorBlend(q31_t value);
	/// @}
	/// @brief Move the params of this owner's learned knobs that a CC moves, found through the mapping index.
	/// @param owner        Whose knobs these are, as the mapping table names them.
	/// @param cable        The cable the CC came from.
	/// @param channel      The CC's channel.
	/// @param ccNumber     The CC number.
	/// @param value        The CC's value.
	/// @param modelStack   The owner's clip; a learned param may clone it for arrangement recording.
	/// @param noteRowIndex The kit row's place in the clip, for a kit row's knobs; -1 otherwise.
	/// @return Whether any knob was learned to the CC.
	bool offerReceivedCCToLearnedParamsForClip(deluge::project::ControlsAddress owner, MIDICable& cable,
	                                           uint8_t channel, uint8_t ccNumber, uint8_t value,
	                                           ModelStackWithTimelineCounter* modelStack, int32_t noteRowIndex = -1);
	/// @brief Move the params of the song's learned knobs that a CC moves, found through the mapping index.
	/// @param cable                         The cable the CC came from.
	/// @param channel                       The CC's channel.
	/// @param ccNumber                      The CC number.
	/// @param value                         The CC's value.
	/// @param modelStackWithThreeMainThings The song, as its own timeline counter.
	/// @return Whether any knob was learned to the CC.
	bool offerReceivedCCToLearnedParamsForSong(MIDICable& cable, uint8_t channel, uint8_t ccNumber, uint8_t value,
	                                           ModelStackWithThreeMainThings* modelStackWithThreeMainThings);
	/// @brief Set the param of this owner's first learned knob that a pitch bend moves (a knob learned to CC 128),
	/// found through the mapping index.
	/// @param owner        Whose knobs these are, as the mapping table names them.
	/// @param cable        The cable the bend came from.
	/// @param channel      The bend's channel.
	/// @param data1        The bend's low seven bits.
	/// @param data2        The bend's high seven bits.
	/// @param modelStack   The owner's clip; a learned param may clone it for arrangement recording.
	/// @param noteRowIndex The kit row's place in the clip, for a kit row's knobs; -1 otherwise.
	/// @return Whether any knob was learned to pitch bend.
	bool offerReceivedPitchBendToLearnedParams(deluge::project::ControlsAddress owner, MIDICable& cable,
	                                           uint8_t channel, uint8_t data1, uint8_t data2,
	                                           ModelStackWithTimelineCounter* modelStack, int32_t noteRowIndex = -1);
	/// @brief Move @p knob's param to where a CC of @p value puts it, under the MIDI takeover mode; a value in the
	/// middle of the range marks the knob as not sending relative changes.
	/// @param knob         One of this owner's learned knobs, which the CC was learned to.
	/// @param cable        The cable the CC came from.
	/// @param value        The CC's value.
	/// @param modelStack   The owner's clip; the param may clone it for arrangement recording.
	/// @param noteRowIndex The kit row's place in the clip, for a kit row's knobs; -1 otherwise.
	void handleLearnedKnob(MIDIKnob& knob, MIDICable& cable, uint8_t value, ModelStackWithTimelineCounter* modelStack,
	                       int32_t noteRowIndex);
	/// @brief The song's handleLearnedKnob: no clip to clone, and the views showing the param are refreshed.
	/// @param knob                          One of the song's learned knobs, which the CC was learned to.
	/// @param cable                         The cable the CC came from.
	/// @param value                         The CC's value.
	/// @param modelStackWithThreeMainThings The song, as its own timeline counter.
	void handleLearnedKnob(MIDIKnob& knob, MIDICable& cable, uint8_t value,
	                       ModelStackWithThreeMainThings* modelStackWithThreeMainThings);
	/// @brief Set @p knob's param from a pitch bend's 14-bit value.
	/// @param knob         One of this owner's learned knobs, which pitch bend was learned to.
	/// @param data1        The bend's low seven bits.
	/// @param data2        The bend's high seven bits.
	/// @param modelStack   The owner's clip; the param may clone it for arrangement recording.
	/// @param noteRowIndex The kit row's place in the clip, for a kit row's knobs; -1 otherwise.
	/// @return Whether the knob's param exists, and was set.
	bool handleLearnedPitchBend(MIDIKnob& knob, uint8_t data1, uint8_t data2, ModelStackWithTimelineCounter* modelStack,
	                            int32_t noteRowIndex);
	/// Learna knob to a particular parameter.
	///
	/// @param cable The source MIDI cable, or null if learning a mod knob
	virtual bool learnKnob(MIDICable* cable, ParamDescriptor paramDescriptor, uint8_t whichKnob, uint8_t modKnobMode,
	                       uint8_t midiChannel, Song* song);
	bool unlearnKnobs(ParamDescriptor paramDescriptor, Song* song);
	virtual void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Song* song) {} // Song may be NULL
	ModelStackWithAutoParam* getParamFromMIDIKnob(MIDIKnob& knob, ModelStackWithThreeMainThings* modelStack) override;

	/// @brief The effect components every owner has, in its chain: a sound's, a clip chain's or the master's.
	/// @return The chain's core.
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
	void switchDelayPingPong();
	/// @brief One press of the delay's feedback knob: digital, analog, off, digital, by one Edit.
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
};
