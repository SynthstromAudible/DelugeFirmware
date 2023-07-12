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

#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "patch_cable_strength.h"
#include "source_selection.h"

#include "definitions.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"
#include "hid/matrix/matrix_driver.h"
#include "model/song/song.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "hid/buttons.h"
#include "model/model_stack.h"
#include "modulation/patch/patch_cable_set.h"
#include "hid/display/oled.h"
#include "util/functions.h"
#include "deluge/model/settings/runtime_feature_settings.h"

namespace menu_item {
extern bool movingCursor;

void PatchCableStrength::beginSession(MenuItem* navigatedBackwardFrom) {
	Decimal::beginSession(navigatedBackwardFrom);

	preferBarDrawing =
	    (runtimeFeatureSettings.get(RuntimeFeatureSettingType::PatchCableResolution) == RuntimeFeatureStateToggle::Off);
}

#if HAVE_OLED
void PatchCableStrength::renderOLED() {

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

	if (soundEditor.numberEditPos != getDefaultEditPos()) {
		preferBarDrawing = false;
	}

	char buffer[12];
	if (preferBarDrawing) {
		int rounded = (soundEditor.currentValue + 50 * (soundEditor.currentValue > 0 ? 1 : -1)) / 100;
		intToString(rounded, buffer, 1);
		OLED::drawStringAlignRight(buffer, extraY + OLED_MAIN_TOPMOST_PIXEL + 4 + destinationDescriptor.isJustAParam(),
		                           OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, 18, 20);

		int marginL = destinationDescriptor.isJustAParam() ? 0 : 80;
		int yBar = destinationDescriptor.isJustAParam() ? 36 : 37;
		drawBar(yBar, marginL, 0);
	}
	else {
		const int digitWidth = TEXT_BIG_SPACING_X;
		const int digitHeight = TEXT_BIG_SIZE_Y;
		intToString(soundEditor.currentValue, buffer, 3);
		int textPixelY = extraY + OLED_MAIN_TOPMOST_PIXEL + 10 + destinationDescriptor.isJustAParam();
		OLED::drawStringAlignRight(buffer, textPixelY, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, digitWidth,
		                           digitHeight);

		int ourDigitStartX = OLED_MAIN_WIDTH_PIXELS - (soundEditor.numberEditPos + 1) * digitWidth;
		OLED::setupBlink(ourDigitStartX, digitWidth, 40, 44, movingCursor);
		OLED::drawVerticalLine(OLED_MAIN_WIDTH_PIXELS - 2 * digitWidth, textPixelY + digitHeight + 1,
		                       textPixelY + digitHeight + 3, OLED::oledMainImage);
		OLED::drawVerticalLine(OLED_MAIN_WIDTH_PIXELS - 2 * digitWidth - 1, textPixelY + digitHeight + 1,
		                       textPixelY + digitHeight + 3, OLED::oledMainImage);
	}
}
#endif

void PatchCableStrength::readCurrentValue() {
	PatchCableSet* patchCableSet = soundEditor.currentParamManager->getPatchCableSet();
	unsigned int c = patchCableSet->getPatchCableIndex(getS(), getDestinationDescriptor());
	if (c == 255) {
		soundEditor.currentValue = 0;
	}
	else {
		int32_t paramValue = patchCableSet->patchCables[c].param.getCurrentValue();
		// the internal values are stored in the range -(2^30) to 2^30.
		// rescale them to the range -5000 to 5000 and round to nearest.
		soundEditor.currentValue = ((int64_t)paramValue * 5000 + (1 << 29)) >> 30;
	}
}

// Might return a ModelStack with NULL autoParam - check for that!
ModelStackWithAutoParam* PatchCableStrength::getModelStack(void* memory, bool allowCreation) {
	ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
	ParamCollectionSummary* paramSetSummary = modelStack->paramManager->getPatchCableSetSummary();

	ModelStackWithParamCollection* modelStackWithParamCollection =
	    modelStack->addParamCollectionSummary(paramSetSummary);
	ModelStackWithParamId* ModelStackWithParamId = modelStackWithParamCollection->addParamId(getLearningThing().data);

	return paramSetSummary->paramCollection->getAutoParamFromId(ModelStackWithParamId, allowCreation);
}

void PatchCableStrength::writeCurrentValue() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStackWithParam = getModelStack(modelStackMemory, true);
	if (!modelStackWithParam->autoParam) {
		return;
	}

	// rescale from 5000 to 2**30. The magic constant is ((2^30)/5000), shifted 32 bits for precision ((1<<(30+32))/5000)
	int32_t finalValue = ((int64_t)922337203685477 * soundEditor.currentValue) >> 32;
	modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(finalValue, modelStackWithParam);
}

int PatchCableStrength::checkPermissionToBeginSession(Sound* sound, int whichThing, MultiRange** currentRange) {

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

uint8_t PatchCableStrength::getIndexOfPatchedParamToBlink() {
	if (soundEditor.patchingParamSelected == PARAM_GLOBAL_VOLUME_POST_REVERB_SEND
	    || soundEditor.patchingParamSelected == PARAM_LOCAL_VOLUME) {
		return PARAM_GLOBAL_VOLUME_POST_FX;
	}
	else {
		return soundEditor.patchingParamSelected;
	}
}

MenuItem* PatchCableStrength::selectButtonPress() {

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
	else {
		return NULL; // Navigate back
	}
}
} // namespace menu_item
