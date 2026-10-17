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
#include "engines/effects/filter.h"
#include "model/fx/host_chain_state.h"
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
	void modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) override;
	bool modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack) override;
	ModelStackWithAutoParam* getParamFromModEncoder(int32_t whichModEncoder, ModelStackWithThreeMainThings* modelStack,
	                                                bool allowCreation = true) override;

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
	bool isEditingComp() override { return editingComp; }
	int32_t getKnobPosForNonExistentParam(int32_t whichModEncoder, ModelStackWithAutoParam* modelStack) override;
	ActionResult modEncoderActionForNonExistentParam(int32_t offset, int32_t whichModEncoder,
	                                                 ModelStackWithAutoParam* modelStack) override;
	/// @brief The chain's filters: a clip chain's or the master's.
	/// @return The filters.
	[[nodiscard]] virtual deluge::effects::Filter& filter() = 0;
	/// @brief The chain's settings and saved view, as its record decodes to.
	[[nodiscard]] const deluge::model::HostChainState& chainState() const { return chainState_; }
	/// @brief Sets the chain's settings and view from what its record decodes to (or, outside the song, directly):
	/// the effects through setEffects, which projects the render copy and makes the chain's hand-overs.
	/// @param next The settings and view to adopt.
	void setChainState(const deluge::model::HostChainState& next);
	/// @brief Binds effects() to the chain state: each derived owner calls it in its constructor, once its chain
	/// exists.
	void bindChainState() { bindEffects(&chainState_.effects); }
	/// @brief What a save writes: the chain's record as it decodes, or the state for an owner outside the current song.
	/// @return The chain state to save.
	[[nodiscard]] deluge::model::HostChainState savedChainState();
	/// @return Which value the mod FX knob edits.
	[[nodiscard]] ModFXParam modFXParam() const { return chainState_.mod_fx_param; }
	/// @return Which filter the filter knobs edit.
	[[nodiscard]] FilterType filterType() const { return chainState_.filter_type; }
	bool editingComp;
	CompParam currentCompParam;

	ModFXType getModFXType() override;
	void projectView(deluge::snapshot::ModControllableSnapshot& copy) override;
	/// @brief The mod FX type in effect.
	/// @param paramManager The owner's param manager.
	/// @return The mod FX type: NONE while the selected mod FX knob is at its minimum (the soft off).
	ModFXType getActiveModFXType(ParamManager* paramManager);

protected:
	int maxCompParam = 0;
	virtual int32_t getParameterFromKnob(int32_t whichModEncoder);

private:
	void ensureModFXParamIsValid();
	/// @brief The compressor threshold the comp-edit knob shows: the render's.
	/// @return The threshold (Q31).
	[[nodiscard]] q31_t displayedCompressorThreshold();
	/// @brief The chain's settings and view; effects() is bound to its effects.
	deluge::model::HostChainState chainState_{};
	void displayCompressorAndReverbSettings(bool on);
	char const* getCompressorModeDisplayName();
	char const* getCompressorParamDisplayName();
	void displayModFXSettings(bool on);
	char const* getModFXTypeDisplayName();
	char const* getModFXParamDisplayName();
};
