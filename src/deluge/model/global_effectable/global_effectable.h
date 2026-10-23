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
#include "deluge-project/fx_chain.h"
#include "engines/effects/filter.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "modulation/arpeggiator.h"
#include "serialization/value.h"
using namespace deluge;
class Serializer;

class GlobalEffectable : public ModControllableAudio {
public:
	GlobalEffectable();
	void cloneFrom(ModControllableAudio* other) override;

	static void initParams(ParamManager* paramManager);
	static void initParamsForAudioClip(ParamManagerForTimeline* paramManager);
	void modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager,
	                     const deluge::project::CompView& comp = {}) override;
	bool modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack,
	                            deluge::project::CompView* comp = nullptr) override;
	ModelStackWithAutoParam* getParamFromModEncoder(int32_t whichModEncoder, ModelStackWithThreeMainThings* modelStack,
	                                                bool allowCreation = true,
	                                                const deluge::project::CompView& comp = {}) override;

	void writeAttributesToFile(Serializer& writer, bool writeToFile);
	/// @brief Writes this owner's tags: its default params, when @p paramManager, then its effect tags and learned
	/// knobs.
	/// @param writer          Destination.
	/// @param paramManager    The params to write, or nullptr for none.
	/// @param writeAutomation Whether the params' automation is written.
	/// @param knobsSong       The song being saved, whose table holds the learned knobs, or nullptr for none.
	/// @param knobsOwner      Whose controls these are: the learned knobs @p knobsSong's table holds for it are
	/// written.
	void writeTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation, Song* knobsSong,
	                     std::optional<deluge::project::ControlsAddress> knobsOwner);
	Error readTagFromFile(Deserializer& reader, char const* tagName, ParamManagerForTimeline* paramManager,
	                      int32_t readAutomationUpToPos, ArpeggiatorSettings* arpSettings, Song* song) override;
	/// @brief Reads this owner's effects and its view (which values its knobs edit) from its element, as the effects
	/// library decodes them, sync levels relative to @p tickMagnitude.
	/// @param element       The owner's serialized element.
	/// @param tickMagnitude Tick magnitude that sync levels are relative to.
	void readEffectsFromElement(const deluge::serialization::Value& element, int32_t tickMagnitude);
	static void writeParamAttributesToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
	                                       int32_t* valuesForOverride = nullptr);
	static void writeParamTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
	                                 int32_t* valuesForOverride = nullptr);
	static void readParamsFromFile(Deserializer& reader, ParamManagerForTimeline* paramManager,
	                               int32_t readAutomationUpToPos);
	/// @brief Reads the param block into the owner's DOM store, then projects each engine RT cursor from the store.
	///
	/// The defaults must already be written to the store by initParams. The DOM thereby holds the authoritative
	/// current_value, not just the automation nodes. Used by audio and kit clips.
	/// @param reader               Source of the param block.
	/// @param paramManager         The owner's param manager.
	/// @param readAutomationUpToPos Automation is read up to this position.
	static void readParamsFromFileDomFirst(Deserializer& reader, ParamManagerForTimeline* paramManager,
	                                       int32_t readAutomationUpToPos);
	static bool readParamTagFromFile(Deserializer& reader, char const* tagName, ParamManagerForTimeline* paramManager,
	                                 int32_t readAutomationUpToPos);
	deluge::effects::Delay::State createDelayWorkingState(ParamManager& paramManager,
	                                                      bool shouldLimitDelayFeedback = false,
	                                                      bool soundComingIn = true);
	int32_t getKnobPosForNonExistentParam(int32_t whichModEncoder, ModelStackWithAutoParam* modelStack,
	                                      const deluge::project::CompView& comp = {}) override;
	ActionResult modEncoderActionForNonExistentParam(int32_t offset, int32_t whichModEncoder,
	                                                 ModelStackWithAutoParam* modelStack,
	                                                 const deluge::project::CompView& comp = {}) override;
	/// @brief The chain's filters: a clip chain's or the master's.
	/// @return The filters.
	[[nodiscard]] virtual deluge::effects::Filter& filter() = 0;
	/// @brief Sets which value the mod FX knob and which filter the filter knobs edit, in the bound record while there
	/// is one, and projects the render copy.
	/// @param param Which mod FX value.
	/// @param type  Which filter.
	void setView(ModFXParam param, FilterType type);
	/// @brief Plays the effects and view @p chain holds from now on, @p chain taking what this owner plays now.
	/// Binding to the chain already bound does nothing.
	/// @param chain The owner's chain record, which outlives the binding (unbindChain() runs before it goes).
	void bindChain(deluge::project::FxChain& chain);
	/// @brief Takes back the effects and view the bound record holds, before the record goes; nothing when unbound.
	void unbindChain();
	/// @brief The chain record this owner plays, or null when it plays its own.
	/// @return The bound record.
	[[nodiscard]] const deluge::project::FxChain* boundChain() const { return boundChain_; }
	[[nodiscard]] const ::GlobalEffectable* asGlobalEffectable() const override { return this; }
	/// @return Which value the mod FX knob edits.
	[[nodiscard]] ModFXParam modFXParam() const;
	/// @return Which filter the filter knobs edit.
	[[nodiscard]] FilterType filterType() const;

	ModFXType getModFXType() override;
	void projectView(deluge::snapshot::ModControllableSnapshot& copy) override;
	/// @brief The mod FX type in effect.
	/// @param paramManager The owner's param manager.
	/// @return The mod FX type: NONE while the selected mod FX knob is at its minimum (the soft off).
	ModFXType getActiveModFXType(ParamManager* paramManager);

protected:
	virtual int32_t getParameterFromKnob(int32_t whichModEncoder, const deluge::project::CompView& comp);

private:
	/// @brief Sets which mod FX value and filter the knobs edit, in the bound record while there is one, without
	/// projecting the render copy.
	/// @param param Which mod FX value.
	/// @param type  Which filter.
	void writeView(ModFXParam param, FilterType type);
	void ensureModFXParamIsValid();
	/// @brief The compressor threshold the comp-edit knob shows: the render's.
	/// @return The threshold (Q31).
	[[nodiscard]] q31_t displayedCompressorThreshold();
	/// @brief Which value the mod FX knob edits while no record holds the view.
	ModFXParam modFXParam_{ModFXParam::FEEDBACK};
	/// @brief Which filter the filter knobs edit while no record holds the view.
	FilterType filterType_{FilterType::LPF};
	/// @brief The record whose effects and view this owner plays, or null.
	deluge::project::FxChain* boundChain_{nullptr};
	void displayCompressorAndReverbSettings(bool on, const deluge::project::CompView& comp);
	static char const* getCompressorModeDisplayName(const deluge::project::CompView& comp);
	static char const* getCompressorParamDisplayName(const deluge::project::CompView& comp);
	void displayModFXSettings(bool on);
	char const* getModFXTypeDisplayName();
	char const* getModFXParamDisplayName();
};
