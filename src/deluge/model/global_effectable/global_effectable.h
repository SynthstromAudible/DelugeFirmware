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
	/// @brief Configures the filters for this block from @p paramManager's params and this owner's filter state.
	/// @param postFXVolume The post-FX volume (Q31), compensated in place for the filters.
	void configureFilter(int32_t* postFXVolume, ParamManager* paramManager);
	/// @brief Filters @p buffer in place, as last configured.
	void processFilter(std::span<StereoSample> buffer);
	/// @param reverbBuffer The block's reverb send buffer, which the grain adds its feed into from index 0.
	void processFXForGlobalEffectable(std::span<StereoSample> buffer, int32_t* postFXVolume, ParamManager* paramManager,
	                                  const Delay::State& delayWorkingState, bool anySoundComingIn, q31_t verbAmount,
	                                  int32_t* reverbBuffer);

	void writeAttributesToFile(Serializer& writer, bool writeToFile);
	void writeTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeToFile);
	Error readTagFromFile(Deserializer& reader, char const* tagName, ParamManagerForTimeline* paramManager,
	                      int32_t readAutomationUpToPos, ArpeggiatorSettings* arpSettings, Song* song) override;
	/// @brief Reads this owner's effects and its view (which values its knobs edit) from its element, as the effects
	/// library decodes them, sync levels relative to @p tickMagnitude.
	void readEffectsFromElement(const deluge::serialization::Value& element, int32_t tickMagnitude);
	static void writeParamAttributesToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
	                                       int32_t* valuesForOverride = nullptr);
	static void writeParamTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
	                                 int32_t* valuesForOverride = nullptr);
	static void readParamsFromFile(Deserializer& reader, ParamManagerForTimeline* paramManager,
	                               int32_t readAutomationUpToPos);
	// Option I (read-authority): read the param block INTO the owner's DOM store (defaults already written there by
	// initParams), then project each engine RT cursor from the store — so the DOM holds the authoritative
	// current_value, not just the automation nodes. Byte-identical to readParamsFromFile. Used by audio + kit clips.
	static void readParamsFromFileDomFirst(Deserializer& reader, ParamManagerForTimeline* paramManager,
	                                       int32_t readAutomationUpToPos);
	static bool readParamTagFromFile(Deserializer& reader, char const* tagName, ParamManagerForTimeline* paramManager,
	                                 int32_t readAutomationUpToPos);
	Delay::State createDelayWorkingState(ParamManager& paramManager, bool shouldLimitDelayFeedback = false,
	                                     bool soundComingIn = true);
	bool isEditingComp() override { return editingComp; }
	int32_t getKnobPosForNonExistentParam(int32_t whichModEncoder, ModelStackWithAutoParam* modelStack) override;
	ActionResult modEncoderActionForNonExistentParam(int32_t offset, int32_t whichModEncoder,
	                                                 ModelStackWithAutoParam* modelStack) override;
	deluge::effects::Filter filter_;
	ModFXParam currentModFXParam;
	FilterType currentFilterType;
	bool editingComp;
	CompParam currentCompParam;

	ModFXType getModFXType() override;

protected:
	int maxCompParam = 0;
	virtual int32_t getParameterFromKnob(int32_t whichModEncoder);
	ModFXType getActiveModFXType(ParamManager* paramManager);

private:
	void ensureModFXParamIsValid();
	void displayCompressorAndReverbSettings(bool on);
	char const* getCompressorModeDisplayName();
	char const* getCompressorParamDisplayName();
	void displayModFXSettings(bool on);
	char const* getModFXTypeDisplayName();
	char const* getModFXParamDisplayName();
};
