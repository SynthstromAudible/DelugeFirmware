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
#include "model/model_stack.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_manager.h"
#include "util/functions.h"

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
	char buffer[5];
	int valueForDisplay = knobPos;
	valueForDisplay += 64;
	if (valueForDisplay == 128) {
		valueForDisplay = 127;
	}
	intToString(valueForDisplay, buffer);
	numericDriver.displayPopup(buffer, 3, true);

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
