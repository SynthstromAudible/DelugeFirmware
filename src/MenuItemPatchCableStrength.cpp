/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include <sound.h>
#include "MenuItemPatchCableStrength.h"
#include "definitions.h"
#include "soundeditor.h"
#include "numericdriver.h"
#include "MenuItemSourceSelection.h"
#include "matrixdriver.h"
#include "song.h"
#include "Action.h"
#include "ActionLogger.h"
#include "Buttons.h"
#include "ModelStack.h"
#include "PatchCableSet.h"
#include "oled.h"
#include "functions.h"

MenuItemPatchCableStrengthRegular patchCableStrengthMenuRegular;
MenuItemPatchCableStrengthRange patchCableStrengthMenuRange;

#if HAVE_OLED
void MenuItemPatchCableStrength::renderOLED() {

	int extraY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 0 : 1;

	int s = getS();

	int yTop = extraY + OLED_MAIN_TOPMOST_PIXEL;
	int ySpacing;

	ParamDescriptor destinationDescriptor = getDestinationDescriptor();
	if (destinationDescriptor.isJustAParam()) {
		yTop += 3;
		ySpacing = TEXT_SPACING_Y;
	}
	else {
		yTop++;
		ySpacing = 8;
	}

	int yPixel = yTop;

	OLED::drawString(getSourceDisplayNameForOLED(s), 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
	                 TEXT_SPACING_X, TEXT_SIZE_Y_UPDATED);
	yPixel += ySpacing;

	if (!destinationDescriptor.isJustAParam()) {
		//OLED::drawGraphicMultiLine(OLED::downArrowIcon, 0, yPixel, 8, OLED::oledMainImage[0]);
		int horizontalLineY = yPixel + (ySpacing << 1);
		OLED::drawVerticalLine(4, yPixel + 1, horizontalLineY, OLED::oledMainImage);
		int rightArrowX = 3 + TEXT_SPACING_X;
		OLED::drawHorizontalLine(horizontalLineY, 4, TEXT_SPACING_X * 2 + 4, OLED::oledMainImage);
		OLED::drawGraphicMultiLine(OLED::rightArrowIcon, rightArrowX, horizontalLineY - 2, 3, OLED::oledMainImage[0]);

		yPixel += ySpacing - 1;

		int s2 = destinationDescriptor.getTopLevelSource();
		OLED::drawString(getSourceDisplayNameForOLED(s2), TEXT_SPACING_X * 2, yPixel - 3, OLED::oledMainImage[0],
		                 OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X, TEXT_SIZE_Y_UPDATED);
		yPixel += ySpacing;

		OLED::drawVerticalLine(TEXT_SPACING_X * 2 + 4, yPixel - 2, yPixel + 2, OLED::oledMainImage);
	}

	OLED::drawGraphicMultiLine(OLED::downArrowIcon, destinationDescriptor.isJustAParam() ? 2 : (TEXT_SPACING_X * 2 + 2),
	                           yPixel, 5, OLED::oledMainImage[0]);
	yPixel += ySpacing;

	int p = destinationDescriptor.getJustTheParam();

	OLED::drawString(getPatchedParamDisplayNameForOled(p), 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
	                 TEXT_SPACING_X, TEXT_SIZE_Y_UPDATED);

	char buffer[12];
	intToString(soundEditor.currentValue, buffer, 1);
	OLED::drawStringAlignRight(buffer, extraY + OLED_MAIN_TOPMOST_PIXEL + 4 + destinationDescriptor.isJustAParam(),
	                           OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, 18, 20);

	int marginL = destinationDescriptor.isJustAParam() ? 0 : 80;
	int yBar = destinationDescriptor.isJustAParam() ? 36 : 37;
	drawBar(yBar, marginL, 0);
}
#endif

void MenuItemPatchCableStrength::readCurrentValue() {
	PatchCableSet* patchCableSet = soundEditor.currentParamManager->getPatchCableSet();
	unsigned int c = patchCableSet->getPatchCableIndex(getS(), getDestinationDescriptor());
	if (c == 255) soundEditor.currentValue = 0;
	else {
		int32_t paramValue = patchCableSet->patchCables[c].param.getCurrentValue();
		soundEditor.currentValue = ((int64_t)paramValue * 50 + 536870912) >> 30;
	}
}

// Might return a ModelStack with NULL autoParam - check for that!
ModelStackWithAutoParam* MenuItemPatchCableStrength::getModelStack(void* memory, bool allowCreation) {
	ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
	ParamCollectionSummary* paramSetSummary = modelStack->paramManager->getPatchCableSetSummary();

	ModelStackWithParamCollection* modelStackWithParamCollection =
	    modelStack->addParamCollectionSummary(paramSetSummary);
	ModelStackWithParamId* ModelStackWithParamId = modelStackWithParamCollection->addParamId(getLearningThing().data);

	return paramSetSummary->paramCollection->getAutoParamFromId(ModelStackWithParamId, allowCreation);
}

void MenuItemPatchCableStrength::writeCurrentValue() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStackWithParam = getModelStack(modelStackMemory, true);
	if (!modelStackWithParam->autoParam) return;

	int32_t finalValue = soundEditor.currentValue * 21474836;
	modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(finalValue, modelStackWithParam);
}

int MenuItemPatchCableStrength::checkPermissionToBeginSession(Sound* sound, int whichThing, MultiRange** currentRange) {

	ParamDescriptor destinationDescriptor = getDestinationDescriptor();
	int s = getS();

	// If patching to another cable's range...
	if (!destinationDescriptor.isJustAParam()) {
		// Global source - can control any range
		if (s < FIRST_LOCAL_SOURCE) {
			return MENU_PERMISSION_YES;
		}

		// Local source - range must be for cable going to local param
		else {
			return (destinationDescriptor.getJustTheParam() < FIRST_GLOBAL_PARAM) ? MENU_PERMISSION_YES
			                                                                      : MENU_PERMISSION_NO;
		}
	}

	int p = destinationDescriptor.getJustTheParam();

	if (!sound->maySourcePatchToParam(
	        s, p,
	        ((ParamManagerForTimeline*)soundEditor
	             .currentParamManager))) { // Note, that requires soundEditor.currentParamManager be set before this is called, which isn't quite ideal.
		return MENU_PERMISSION_NO;
	}

	return MENU_PERMISSION_YES;
}

uint8_t MenuItemPatchCableStrength::getIndexOfPatchedParamToBlink() {
	if (soundEditor.patchingParamSelected == PARAM_GLOBAL_VOLUME_POST_REVERB_SEND
	    || soundEditor.patchingParamSelected == PARAM_LOCAL_VOLUME)
		return PARAM_GLOBAL_VOLUME_POST_FX;
	else return soundEditor.patchingParamSelected;
}

MenuItem* MenuItemPatchCableStrength::selectButtonPress() {

	// If shift held down, delete automation
	if (Buttons::isShiftButtonPressed()) {
		Action* action = actionLogger.getNewAction(ACTION_AUTOMATION_DELETE, false);

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* modelStack = getModelStack(modelStackMemory);
		if (modelStack->autoParam) {
			modelStack->autoParam->deleteAutomation(action, modelStack);
		}

		numericDriver.displayPopup(HAVE_OLED ? "Automation deleted" : "DELETED");
		return (MenuItem*)0xFFFFFFFF; // No navigation
	}
	else return NULL; // Navigate back
}

// MenuItemPatchCableStrengthRegular ----------------------------------------------------------------------------

#if !HAVE_OLED
void MenuItemPatchCableStrengthRegular::drawValue() {

	PatchCableSet* patchCableSet = soundEditor.currentParamManager->getPatchCableSet();

	uint8_t drawDot = patchCableSet->doesDestinationDescriptorHaveAnyCables(getLearningThing()) ? 3 : 255;

	numericDriver.setTextAsNumber(soundEditor.currentValue, drawDot);
}
#endif

MenuItem* MenuItemPatchCableStrengthRegular::selectButtonPress() {

	// If shift held down, delete automation
	if (Buttons::isShiftButtonPressed()) {
		return MenuItemPatchCableStrength::selectButtonPress();
	}
	else {
		return &sourceSelectionMenuRange;
	}
}

ParamDescriptor MenuItemPatchCableStrengthRegular::getLearningThing() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamAndSource(soundEditor.patchingParamSelected, sourceSelectionMenuRegular.s);
	return paramDescriptor;
}

ParamDescriptor MenuItemPatchCableStrengthRegular::getDestinationDescriptor() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(soundEditor.patchingParamSelected);
	return paramDescriptor;
}

uint8_t MenuItemPatchCableStrengthRegular::getS() {
	return sourceSelectionMenuRegular.s;
}

int MenuItemPatchCableStrengthRegular::checkPermissionToBeginSession(Sound* sound, int whichThing,
                                                                     MultiRange** currentRange) {

	if (soundEditor.patchingParamSelected == PARAM_GLOBAL_VOLUME_POST_FX) {
		if (!sound->maySourcePatchToParam(getS(), soundEditor.patchingParamSelected,
		                                  ((ParamManagerForTimeline*)soundEditor.currentParamManager))) {
			soundEditor.patchingParamSelected = PARAM_GLOBAL_VOLUME_POST_REVERB_SEND;
			if (!sound->maySourcePatchToParam(getS(), soundEditor.patchingParamSelected,
			                                  ((ParamManagerForTimeline*)soundEditor.currentParamManager))) {
				soundEditor.patchingParamSelected = PARAM_LOCAL_VOLUME;
			}
		}
	}

	return MenuItemPatchCableStrength::checkPermissionToBeginSession(sound, whichThing, currentRange);
}

uint8_t MenuItemPatchCableStrengthRegular::shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour) {

	// If this is the actual source we're editing for...
	if (s == getS()) return 0;

	PatchCableSet* patchCableSet = soundEditor.currentParamManager->getPatchCableSet();

	// Or, if it's the source controlling the range of the source we're editing for...
	if (patchCableSet->getPatchCableIndex(s, getLearningThing()) != 255) {
		*colour = 0b110;
		return 3;
	}

	else return 255;
}

MenuItem* MenuItemPatchCableStrengthRegular::patchingSourceShortcutPress(int s, bool previousPressStillActive) {
	if (previousPressStillActive) {
		sourceSelectionMenuRange.s = s;
		return &patchCableStrengthMenuRange;
	}
	else return (MenuItem*)0xFFFFFFFF;
}

// MenuItemPatchCableStrengthRange ----------------------------------------------------------------------------

#if !HAVE_OLED
void MenuItemPatchCableStrengthRange::drawValue() {
	numericDriver.setTextAsNumber(soundEditor.currentValue);
}
#endif

ParamDescriptor MenuItemPatchCableStrengthRange::getLearningThing() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamAndTwoSources(soundEditor.patchingParamSelected, sourceSelectionMenuRegular.s,
	                                            sourceSelectionMenuRange.s);
	return paramDescriptor;
}

ParamDescriptor MenuItemPatchCableStrengthRange::getDestinationDescriptor() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamAndSource(soundEditor.patchingParamSelected, sourceSelectionMenuRegular.s);
	return paramDescriptor;
}

uint8_t MenuItemPatchCableStrengthRange::getS() {
	return sourceSelectionMenuRange.s;
}

uint8_t MenuItemPatchCableStrengthRange::shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour) {

	// If this is the actual source we're editing for...
	if (s == getS()) {
		*colour = 0b110;
		return 0;
	}

	// Or, if it's the source whose range we are controlling...
	else if (sourceSelectionMenuRegular.s == s) return 3; // Did I get this right? #patchingoverhaul2021

	else return 255;
}

MenuItem* MenuItemPatchCableStrengthRange::patchingSourceShortcutPress(int newS, bool previousPressStillActive) {
	return (MenuItem*)0xFFFFFFFF;
}

// MenuItemFixedPatchCableStrength ----------------------------------------------------------------------------

int MenuItemFixedPatchCableStrength::checkPermissionToBeginSession(Sound* sound, int whichThing,
                                                                   MultiRange** currentRange) {
	soundEditor.patchingParamSelected = p;
	sourceSelectionMenuRegular.s = s;
	return MenuItemPatchCableStrength::checkPermissionToBeginSession(sound, whichThing, currentRange);
}

uint8_t MenuItemFixedPatchCableStrength::shouldBlinkPatchingSourceShortcut(int s, uint8_t* sourceShortcutBlinkColours) {

	PatchCableSet* patchCableSet = soundEditor.currentParamManager->getPatchCableSet();

	// If it's the source controlling the range of the source we're editing for...
	if (patchCableSet->getPatchCableIndex(s, getLearningThing()) != 255) return 3;

	else return 255;
}

MenuItem* MenuItemFixedPatchCableStrength::patchingSourceShortcutPress(int s, bool previousPressStillActive) {
	sourceSelectionMenuRange.s = s;
	return &patchCableStrengthMenuRange;
}
