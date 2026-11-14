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
#include "deluge-project/mapping_table.h"
#include "dsp-next/core/stereo_sample.h"
#include "effects/delay.h"
#include "hid/button.h"
#include "model/fx/effects_binding.h"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/mod_controllable/mod_controllable.h"
#include "model/snapshot/mod_controllable_snapshot.h"
#include "modulation/knob_target.h"
#include "modulation/params/param_set.h"
#include "modulation/sync.h"
#include "synth/modulation/cable.h"
#include "synth/modulation/lfo.h"
#include "util/functions.h"
#include <optional>
#include <vector>

#include "effects/chain_core.h"
#include "effects/state.h"
#include "plugin-api/plugin.h"

class ArpeggiatorSettings;
class Clip;
class GlobalEffectable;
class Sound;
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
	                     std::optional<int32_t> compressorThreshold = std::nullopt);
	/// @brief Writes the learned MIDI knobs' tag, which a file writes between the delay and the other effect tags: the
	/// knobs @p song's mapping table holds for @p owner, in table order. An owner @p song does not hold (a preset
	/// loaded outside it) writes those it holds outside a table (loaded_knobs), and none when it holds none.
	/// @param writer Destination.
	/// @param song   The song being saved, or the one a preset is saved from; nullptr when there is none.
	/// @param owner  Whose controls these are, if known.
	void writeMidiKnobsToFile(Serializer& writer, Song* song, std::optional<deluge::project::ControlsAddress> owner);
	/// @brief The codec context a file's effect entries are written in: the current song's tick magnitude, even when
	/// the song being saved is another (the learned knobs take the saved song; this does not).
	/// @return The codec context.
	[[nodiscard]] static deluge::plugin::CodecContext effectsCodecContext();
	/// @brief The compressor threshold a sound's file saves: the one the render last applied, its param's.
	/// @return The threshold (Q31).
	[[nodiscard]] int32_t savedCompressorThreshold() const { return core().compressor().dsp().threshold().raw(); }

	/// @brief This owner's effect settings, as its binding holds them: a bound record's, else the binding's own.
	/// @return The effect settings.
	[[nodiscard]] const deluge::effects::EffectStates& effects() const { return binding_->effects(); }
	/// @brief Make @p next this owner's effect settings, and the render's copy of them, which is replaced with the
	/// render excluded.
	/// @param next The new effect settings.
	void setEffects(const deluge::effects::EffectStates& next);
	/// @brief Keep this owner's effect settings in @p record (a sound's SoundState), which takes the settings held now;
	/// nullptr keeps them here again.
	/// @param record The record that holds the settings, or nullptr to hold them in this owner.
	void bindEffects(deluge::effects::EffectStates* record);
	/// @brief The render's copy of the effect settings: it reads nothing else of them.
	/// @return The render's copy.
	[[nodiscard]] const deluge::snapshot::ModControllableSnapshot& modControllableSnapshot() const {
		return binding_->snapshot();
	}
	/// @brief Copy the settings into the render's copy, with the render excluded.
	void projectModControllableSnapshot();
	/// @brief The view projectModControllableSnapshot copies into the render's copy: a host chain's selected mod FX
	/// knob, which its soft off reads; nullopt for a sound.
	/// @return The selected mod FX knob, or nullopt.
	[[nodiscard]] virtual std::optional<ModFXParam> projectedModFXParam() const { return std::nullopt; }
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
	/// @brief Sets the shared params (EQ, arpeggiator, stutter, mod FX, bitcrush, compressor) to their defaults.
	/// @param paramManager The param manager to initialise.
	/// @param defaults     Whose table's defaults: UNPATCHED_SOUND for a sound, UNPATCHED_GLOBAL for a global
	/// effectable.
	static void initParams(ParamManager* paramManager, deluge::modulation::params::Kind defaults);
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
	// The reverb's sidechain (AudioEngine::reverb.sidechain()) is the engine's own, not an owner's.
	void setSidechainAttack(int32_t value);
	void setSidechainRelease(int32_t value);
	void setSidechainSyncType(SyncType value);
	void setSidechainSyncLevel(SyncLevel value);
	// The compressor's knob positions, each Q31 (the menu's 0-127 shifted up by 24).
	void setCompressorAttack(int32_t value);
	void setCompressorRelease(int32_t value);
	void setCompressorThreshold(int32_t value);
	void setCompressorRatio(int32_t value);
	void setCompressorSidechainHpf(int32_t value);
	void setCompressorBlend(int32_t value);
	/// @}
	/// @brief Learn mod knob @p whichKnob of mode @p modKnobMode to @p target. Only a sound has mod knobs; a learned
	/// MIDI knob lives in the song's mapping table.
	/// @param target      The param the knob turns.
	/// @param whichKnob   The mod knob: 0 lower, 1 upper.
	/// @param modKnobMode The mod knob mode it is learned in.
	/// @param song        The song the owner belongs to, or nullptr.
	/// @return Whether the knob was learned.
	virtual bool learnModKnob(deluge::modulation::KnobTarget target, uint8_t whichKnob, uint8_t modKnobMode,
	                          Song* song) {
		return false;
	}
	virtual void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Song* song) {} // Song may be NULL

	/// @brief The effect components every owner has, in its chain: a sound's, a clip chain's or the master's.
	/// @return The chain's core.
	[[nodiscard]] virtual deluge::effects::ChainCore& core() = 0;
	[[nodiscard]] virtual const deluge::effects::ChainCore& core() const = 0;

	/// @brief The learned knobs this owner holds outside a song's table: those its file's `midiKnobs` read, in file
	/// order, each target's owner a placeholder. The table takes them when the owner joins a song (a song's load, an
	/// arrival, a preset change's hand-over), and they are emptied then; an owner a song holds keeps its knobs in the
	/// table alone. A save of an owner no song holds writes these.
	std::vector<deluge::project::Mapping> loaded_knobs{};

	/// @brief The output whose learned knobs this one, a clone the render made, takes in the table, until the app loop
	/// copies them there (bridge::cloneOutputMappings). Should the render's held copy be lost, the drain that finds the
	/// loss copies them from it, before any held forget lands.
	std::optional<uint32_t> knobs_cloned_from{};

private:
	/// The binding that holds this owner's settings and the render's copy; set first in each owner's constructor.
	deluge::model::EffectsBinding* binding_{nullptr};

protected:
	/// @brief Ends the song's stutter if this owner's chain plays it, so no later chain at its address plays the tape.
	/// Each owner calls it at the end of its destructor's body, while its chain still exists.
	void endStutterIfStuttering();
	/// @brief Makes @p binding the one this owner's settings and render copy live in. Each owner calls it first in its
	/// constructor, before anything reads its effects.
	/// @param binding The owner's binding, which outlives every use through this owner.
	void bindBinding(deluge::model::EffectsBinding& binding) { binding_ = &binding; }
	/// @brief Gives the delay's tapes back to the arena and leaves its margin, as a change to OFF does. A sound calls
	/// it in its destructor, while its chain still exists; a host chain's channel does the same for itself.
	void releaseChainMemory();

	/// What kind of unpatched parameters this ModControllable uses.
	///
	/// This should be UNPATCHED_GLOBAL for GlobalEffectable and UNPATCHED_SOUND for Sound. If a new ModControllable
	/// subclass is
	deluge::modulation::params::Kind unpatchedParamKind_;
};
