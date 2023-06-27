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

#ifndef GLOBALEFFECTABLE_H_
#define GLOBALEFFECTABLE_H_

#include "ModControllableAudio.h"
#include "FilterSet.h"

class GlobalEffectable : public ModControllableAudio {
public:
	GlobalEffectable();
	void cloneFrom(ModControllableAudio* other);

	static void initParams(ParamManager* paramManager);
	static void initParamsForAudioClip(ParamManagerForTimeline* paramManager);
	void modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager);
	bool modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack);
	ModelStackWithAutoParam* getParamFromModEncoder(int whichModEncoder, ModelStackWithThreeMainThings* modelStack,
	                                                bool allowCreation = true);
	void setupFilterSetConfig(FilterSetConfig* filterSetConfig, int32_t* postFXVolume, ParamManager* paramManager);
	void processFilters(StereoSample* buffer, int numSamples, FilterSetConfig* filterSetConfig);
	void compensateVolumeForResonance(ParamManagerForTimeline* paramManager);
	void processFXForGlobalEffectable(StereoSample* inputBuffer, int numSamples, int32_t* postFXVolume,
	                                  ParamManager* paramManager, DelayWorkingState* delayWorkingState,
	                                  int analogDelaySaturationAmount);

	void writeAttributesToFile(bool writeToFile);
	void writeTagsToFile(ParamManager* paramManager, bool writeToFile);
	int readTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos,
	                    Song* song);
	static void writeParamAttributesToFile(ParamManager* paramManager, bool writeAutomation,
	                                       int32_t* valuesForOverride = NULL);
	static void writeParamTagsToFile(ParamManager* paramManager, bool writeAutomation,
	                                 int32_t* valuesForOverride = NULL);
	static void readParamsFromFile(ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos);
	static bool readParamTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager,
	                                 int32_t readAutomationUpToPos);
	char const* paramToString(uint8_t param);
	int stringToParam(char const* string);
	void setupDelayWorkingState(DelayWorkingState* delayWorkingState, ParamManager* paramManager,
	                            bool shouldLimitDelayFeedback = false);

	FilterSet filterSets[2];
	uint8_t currentModFXParam;
	uint8_t currentFilterType;

protected:
	virtual int getParameterFromKnob(int whichModEncoder);
	int getActiveModFXType(ParamManager* paramManager);

private:
	void ensureModFXParamIsValid();
};

#endif /* GLOBALEFFECTABLE_H_ */
