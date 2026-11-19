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

#include "modulation/params/param_collection.h"
#include "definitions_cxx.hpp"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/song/song.h"
#include "model/timeline_counter.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_control.h"
#include "modulation/params/param_manager.h"
#include "playback/playback_handler.h"

ParamCollection::ParamCollection(int32_t newObjectSize, ParamCollectionSummary* summary) : objectSize(newObjectSize) {
	for (int32_t i = 0; i < kMaxNumUnsignedIntegerstoRepAllParams; i++) { // Just do both even if we're only using one.
		summary->whichParamsAreAutomated[i] = 0;
	}

	for (int32_t i = 0; i < kMaxNumUnsignedIntegerstoRepAllParams; i++) { // Just do both even if we're only using one.
		summary->whichParamsAreInterpolating[i] = 0;
	}
}

ParamCollection::~ParamCollection() {
}

bool ParamCollection::controlsEditClipValue(ModelStackWithThreeMainThings const* modelStack) {
	bool recording_automation = playbackHandler.isEitherClockActive() && (playbackHandler.ticksLeftInCountIn == 0)
	                            && playbackHandler.recording != RecordingMode::OFF && modelStack->timelineCounterIsSet()
	                            && modelStack->getTimelineCounter()->armedForRecording
	                            && modelStack->getTimelineCounter()->isPlayingAutomationNow();
	return !recording_automation;
}

Output* ParamCollection::getLinkedOutput(ModelStackWithThreeMainThings const* modelStack) {
	auto* timeline = modelStack->getTimelineCounterAllowNull();
	if ((timeline == nullptr) || timeline == modelStack->song || !controlsEditClipValue(modelStack)) {
		return nullptr;
	}
	auto* output = static_cast<Clip*>(timeline)->output;
	return (output != nullptr) && output->trackLink ? output : nullptr;
}

int32_t ParamCollection::getValueForControl(ModelStackWithAutoParam const* modelStack) {
	using namespace deluge::modulation::params;
	auto* clip = getClipValueStorage(modelStack->paramId);
	auto layer = controlLayer(getParamKind(), clip != nullptr, controlsEditClipValue(modelStack),
	                          getLinkedOutput(modelStack) != nullptr);
	if (layer == ControlLayer::PRESET) {
		return clip->getPresetValue();
	}
	return layer == ControlLayer::CLIP ? clip->getValue() : modelStack->autoParam->getCurrentValue();
}

void ParamCollection::setValueFromControl(int32_t value, ModelStackWithAutoParam const* modelStack) {
	using namespace deluge::modulation::params;
	auto* clip = getClipValueStorage(modelStack->paramId);
	auto* output = getLinkedOutput(modelStack);
	auto layer = controlLayer(getParamKind(), clip != nullptr, controlsEditClipValue(modelStack), output != nullptr);
	if (layer == ControlLayer::PRESET) {
		Error error = output->setPresetParameter(modelStack->song, modelStack->modControllable, getParamKind(),
		                                         modelStack->paramId, value);
		if (error != Error::NONE) {
			display->displayError(error);
		}
		return;
	}
	if (layer == ControlLayer::LIVE || !modelStack->autoParam->isAutomated()) {
		modelStack->autoParam->setCurrentValueInResponseToUserInput(value, modelStack);
		return;
	}
	if (value == clip->getValue()) {
		return;
	}
	actionLogger.recordUnautomatedParamChange(modelStack);
	int32_t old_value = modelStack->autoParam->getCurrentValue();
	clip->setValue(value);
	view.notifyParamAutomationOccurred(modelStack->paramManager);
	notifyParamModifiedInSomeWay(modelStack, old_value, false, true, true);
}

void ParamCollection::notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
                                                   bool automationChanged, bool automatedBefore, bool automatedNow) {

	bool currentValueChanged = (oldValue != modelStack->autoParam->getCurrentValue());
	if (currentValueChanged || automationChanged) {
		modelStack->paramManager->notifyParamModifiedInSomeWay(modelStack, currentValueChanged, automationChanged,
		                                                       automatedNow);
	}

	if (automationChanged && automatedNow) {
		ticksTilNextEvent = 0;
	}
}

bool ParamCollection::mayParamInterpolate(int32_t paramId) {
	return true;
}

int32_t ParamCollection::paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack) {
	if (paramValue >= (int32_t)(0x80000000 - (1 << 24))) {
		return 64;
	}
	return (paramValue + (1 << 24)) >> 25;
}

int32_t ParamCollection::knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) {
	int32_t paramValue = 2147483647;
	if (knobPos < 64) {
		paramValue = knobPos << 25;
	}
	return paramValue;
}

void ParamCollection::setPlayPos(uint32_t pos, ModelStackWithParamCollection* modelStack, bool reversed) {
	ticksTilNextEvent = 0;
}

void ParamCollection::notifyPingpongOccurred(ModelStackWithParamCollection* modelStack) {
	ticksTilNextEvent = 0;
}
