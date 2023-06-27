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

#include <InstrumentClip.h>
#include "MenuItemPatchedParam.h"
#include "soundeditor.h"
#include <math.h>
#include "song.h"
#include "MenuItemSourceSelection.h"
#include "MenuItemPatchCableStrength.h"
#include "numericdriver.h"
#include "matrixdriver.h"
#include <string.h>
#include "Buttons.h"
#include "ModelStack.h"
#include "ParamSet.h"
#include "PatchCableSet.h"

extern "C" {
#include "cfunctions.h"
}

MenuItem* MenuItemPatchedParam::selectButtonPress() {

	// If shift held down, user wants to delete automation
	if (Buttons::isShiftButtonPressed()) {
		return MenuItemParam::selectButtonPress();
	}
	else {
#if 0 && HAVE_OLED
		return NULL;
#else
		soundEditor.patchingParamSelected = getP();
		return &sourceSelectionMenuRegular;
#endif
	}
}

#if !HAVE_OLED
void MenuItemPatchedParam::drawValue() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(getP());
	uint8_t drawDot =
	    soundEditor.currentParamManager->getPatchCableSet()->isAnySourcePatchedToParamVolumeInspecific(paramDescriptor)
	        ? 3
	        : 255;
	numericDriver.setTextAsNumber(soundEditor.currentValue, drawDot);
}
#endif

uint8_t MenuItemPatchedParam::shouldDrawDotOnName() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(getP());
	return soundEditor.currentParamManager->getPatchCableSet()->isAnySourcePatchedToParamVolumeInspecific(
	           paramDescriptor)
	           ? 3
	           : 255;
}

ParamDescriptor MenuItemPatchedParam::getLearningThing() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(getP());
	return paramDescriptor;
}

ParamSet* MenuItemPatchedParam::getParamSet() {
	return soundEditor.currentParamManager->getPatchedParamSet();
}

uint8_t MenuItemPatchedParam::getPatchedParamIndex() {
	return getP();
}

uint8_t MenuItemPatchedParam::shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour) {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(getP());
	return soundEditor.currentParamManager->getPatchCableSet()->isSourcePatchedToDestinationDescriptorVolumeInspecific(
	           s, paramDescriptor)
	           ? 3
	           : 255;
}

MenuItem* MenuItemPatchedParam::patchingSourceShortcutPress(int s, bool previousPressStillActive) {
	soundEditor.patchingParamSelected = getP();
	sourceSelectionMenuRegular.s = s;
	return &patchCableStrengthMenuRegular;
}

ModelStackWithAutoParam* MenuItemPatchedParam::getModelStack(void* memory) {
	ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
	ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
	int p = getP();
	return modelStack->addParam(summary->paramCollection, summary, p,
	                            &((ParamSet*)summary->paramCollection)->params[p]);
}

// ---------------------------------------

void MenuItemPatchedParamInteger::readCurrentValue() {
	soundEditor.currentValue =
	    (((int64_t)soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP()) + 2147483648) * 50
	     + 2147483648)
	    >> 32;
}

void MenuItemPatchedParamInteger::writeCurrentValue() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStack = getModelStack(modelStackMemory);
	modelStack->autoParam->setCurrentValueInResponseToUserInput(getFinalValue(), modelStack);

	//((ParamManagerBase*)soundEditor.currentParamManager)->setPatchedParamValue(getP(), getFinalValue(), 0xFFFFFFFF, 0, soundEditor.currentSound, currentSong, currentSong->currentClip, true, true);
}

int32_t MenuItemPatchedParamInteger::getFinalValue() {
	if (soundEditor.currentValue == 25) return 0;
	else return (uint32_t)soundEditor.currentValue * 85899345 - 2147483648;
}

// --------------------------------------

uint8_t MenuItemSourceDependentPatchedParam::getP() {
	return MenuItemPatchedParam::getP() + soundEditor.currentSourceIndex;
}

// ---------------------------------------
#if !HAVE_OLED
void MenuItemPatchedParamPan::drawValue() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(getP());
	uint8_t drawDot =
	    soundEditor.currentParamManager->getPatchCableSet()->isAnySourcePatchedToParamVolumeInspecific(paramDescriptor)
	        ? 3
	        : 255;
	char buffer[5];
	intToString(std::abs(soundEditor.currentValue), buffer, 1);
	if (soundEditor.currentValue < 0) strcat(buffer, "L");
	else if (soundEditor.currentValue > 0) strcat(buffer, "R");
	numericDriver.setText(buffer, true, drawDot);
}
#endif

int32_t MenuItemPatchedParamPan::getFinalValue() {
	if (soundEditor.currentValue == 32) return 2147483647;
	else if (soundEditor.currentValue == -32) return -2147483648;
	else return ((int32_t)soundEditor.currentValue * 33554432 * 2);
}

void MenuItemPatchedParamPan::readCurrentValue() {
	soundEditor.currentValue =
	    ((int64_t)soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP()) * 64 + 2147483648) >> 32;
}
