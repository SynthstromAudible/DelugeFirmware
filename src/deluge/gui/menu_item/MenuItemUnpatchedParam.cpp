/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include <AudioEngine.h>
#include <InstrumentClip.h>
#include "MenuItemUnpatchedParam.h"
#include "soundeditor.h"
#include "song.h"
#include "numericdriver.h"
#include "ModelStack.h"
#include "ParamSet.h"

extern "C" {
#include "cfunctions.h"
}

MenuItemUnpatchedParam::MenuItemUnpatchedParam() {
	// TODO Auto-generated constructor stub
}

void MenuItemUnpatchedParam::readCurrentValue() {
	soundEditor.currentValue =
	    (((int64_t)soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP()) + 2147483648) * 50
	     + 2147483648)
	    >> 32;
}

ModelStackWithAutoParam* MenuItemUnpatchedParam::getModelStack(void* memory) {
	ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
	ParamCollectionSummary* summary = modelStack->paramManager->getUnpatchedParamSetSummary();
	int p = getP();
	return modelStack->addParam(summary->paramCollection, summary, p,
	                            &((ParamSet*)summary->paramCollection)->params[p]);
}

void MenuItemUnpatchedParam::writeCurrentValue() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStackWithParam = getModelStack(modelStackMemory);
	modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(getFinalValue(), modelStackWithParam);
}

int32_t MenuItemUnpatchedParam::getFinalValue() {
	if (soundEditor.currentValue == 25) return 0;
	else return (uint32_t)soundEditor.currentValue * 85899345 - 2147483648;
}

ParamDescriptor MenuItemUnpatchedParam::getLearningThing() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(getP() + PARAM_UNPATCHED_SECTION);
	return paramDescriptor;
}

ParamSet* MenuItemUnpatchedParam::getParamSet() {
	return soundEditor.currentParamManager->getUnpatchedParamSet();
}

// ---------------------------------------
void MenuItemUnpatchedParamPan::drawValue() { // TODO: should really combine this with the "patched" version
	uint8_t drawDot = 255;                    //soundEditor.doesParamHaveAnyCables(getP()) ? 3 : 255;
	char buffer[5];
	intToString(std::abs(soundEditor.currentValue), buffer, 1);
	if (soundEditor.currentValue < 0) strcat(buffer, "L");
	else if (soundEditor.currentValue > 0) strcat(buffer, "R");
	numericDriver.setText(buffer, true, drawDot);
}

int32_t MenuItemUnpatchedParamPan::getFinalValue() {
	if (soundEditor.currentValue == 32) return 2147483647;
	else if (soundEditor.currentValue == -32) return -2147483648;
	else return ((int32_t)soundEditor.currentValue * 33554432 * 2);
}

void MenuItemUnpatchedParamPan::readCurrentValue() {
	soundEditor.currentValue =
	    ((int64_t)soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP()) * 64 + 2147483648) >> 32;
}

// ---------------------------------------
void MenuItemUnpatchedParamUpdatingReverbParams::writeCurrentValue() {
	MenuItemUnpatchedParam::writeCurrentValue();
	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
}
