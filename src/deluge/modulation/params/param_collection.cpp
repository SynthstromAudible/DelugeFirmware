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
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/fx/stutterer.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/song/song.h"
#include "model/timeline_counter.h"
#include "modulation/automation/auto_param.h"
#include "modulation/automation/auto_param_access.h"
#include "modulation/params/param_control.h"
#include "modulation/params/param_control_value.h"
#include "modulation/params/param_manager.h"
#include "playback/playback_handler.h"
#include <cstring>

ParamCollection::ParamCollection(int32_t newObjectSize, ParamCollectionSummary* summary) : objectSize(newObjectSize) {
	for (int32_t i = 0; i < kMaxNumUnsignedIntegerstoRepAllParams; i++) { // Just do both even if we're only using one.
		summary->whichParamsAreAutomated[i] = 0;
	}

	for (int32_t i = 0; i < kMaxNumUnsignedIntegerstoRepAllParams; i++) { // Just do both even if we're only using one.
		summary->whichParamsAreInterpolating[i] = 0;
	}
}

ParamCollection::~ParamCollection() {
	if (rampSteps_ != nullptr) {
		delugeDealloc(rampSteps_);
	}
}

int32_t* ParamCollection::allocateRampSteps(int32_t count) {
	void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(count * sizeof(int32_t));
	if (memory != nullptr) {
		std::memset(memory, 0, count * sizeof(int32_t));
		rampSteps_ = static_cast<int32_t*>(memory);
	}
	return rampSteps_;
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

Output* ParamCollection::getLinkedOutput(ModelStackWithThreeMainThings const* modelStack,
                                         deluge::modulation::params::Kind kind, int32_t id) {
	using deluge::modulation::params::Kind;
	Output* output = getLinkedOutput(modelStack);
	if (output != nullptr && id == deluge::modulation::params::UNPATCHED_STUTTER_RATE
	    && (kind == Kind::UNPATCHED_SOUND || kind == Kind::UNPATCHED_GLOBAL)
	    && stutterer.isQuantizedStutter(static_cast<ModControllableAudio*>(modelStack->modControllable))) {
		return nullptr;
	}
	return output;
}

Output* ParamCollection::getLinkedOutput(ModelStackWithParamId const* modelStack) {
	// A knob with nothing assigned, e.g. a MIDI clip's knob without a CC, has no collection.
	if (modelStack->paramCollection == nullptr) {
		return getLinkedOutput(static_cast<ModelStackWithThreeMainThings const*>(modelStack));
	}
	return getLinkedOutput(modelStack, modelStack->paramCollection->getParamKind(), modelStack->paramId);
}

int32_t ParamCollection::getValueForControl(ModelStackWithParamId const* modelStack) {
	return getValueForControl(modelStack->paramId, modelStack);
}

void ParamCollection::setValueFromControl(int32_t value, ModelStackWithParamId const* modelStack, int32_t pos,
                                          int32_t length, bool mayDeleteNodesInLinearRun) {
	using namespace deluge::modulation::params;
	auto* clip = getClipValueStorage(modelStack->paramId);
	auto* output = getLinkedOutput(modelStack);
	auto layer = controlLayer(getParamKind(), true, controlsEditClipValue(modelStack), output != nullptr,
	                          length != 0 && modelStack->timelineCounterIsSet());
	if (layer == ControlLayer::PRESET) {
		Error error = output->setPresetParameter(modelStack->song, modelStack->modControllable, getParamKind(),
		                                         modelStack->paramId, value);
		if (error != Error::NONE) {
			display->displayError(error);
		}
		return;
	}
	// Avoid even resolving AutoParam for a scalar edit which changes nothing.
	if (layer == ControlLayer::CLIP && clip != nullptr && value == clip->getValue()) {
		return;
	}
	auto* param_stack = getAutoParamFromId(modelStack->addAutoParam(nullptr), true);
	if (param_stack->autoParam == nullptr) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return;
	}
	if (layer == ControlLayer::REGION) {
		param_stack->autoParam->automation().setValueForRegion(pos, length, value, param_stack);
		return;
	}
	if (layer == ControlLayer::LIVE || !param_stack->autoParam->isAutomated()) {
		AutoParamLiveInput(*param_stack->autoParam)
		    .setValueForLiveInput(value, param_stack, -1, mayDeleteNodesInLinearRun);
		return;
	}
	actionLogger.recordUnautomatedParamChange(param_stack);
	int32_t old_value =
	    setClipValueFromControl(*getClipValueStorage(modelStack->paramId), param_stack->autoParam->data(), value);
	view.notifyParamAutomationOccurred(param_stack->paramManager);
	notifyParamModifiedInSomeWay(param_stack, old_value, false, true, true);
}

void ParamCollection::notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
                                                   bool automationChanged, bool automatedBefore, bool automatedNow) {
	if (automatedBefore && !automatedNow) {
		AutoParamPlayback(*modelStack->autoParam).clearAutomationOffset();
	}
	notifyParamValueModified(modelStack, oldValue, modelStack->autoParam->getCurrentValue(), automationChanged,
	                         automatedBefore, automatedNow);
}

void ParamCollection::notifyParamValueModified(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
                                               int32_t newValue, bool automationChanged,
                                               [[maybe_unused]] bool automatedBefore, bool automatedNow) {

	bool current_value_changed = (oldValue != newValue);
	if (current_value_changed || automationChanged) {
		modelStack->paramManager->notifyParamModifiedInSomeWay(modelStack, static_cast<int32_t>(current_value_changed),
		                                                       automationChanged, automatedNow);
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
