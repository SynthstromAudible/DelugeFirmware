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

#include "patch_cable_strength.h"
#include "gui/l10n/l10n.h"
#include "gui/menu_item/menu_item.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "source_selection.h"

#include "definitions_cxx.hpp"
#include "deluge/model/settings/runtime_feature_settings.h"
#include "gui/ui/sound_editor.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/matrix/matrix_driver.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/patch/patch_cable_set.h"
#include "util/functions.h"

namespace deluge::gui::menu_item {
extern bool movingCursor;

void PatchCableStrength::beginSession(MenuItem* navigatedBackwardFrom) {
	Decimal::beginSession(navigatedBackwardFrom);

	preferBarDrawing =
	    (runtimeFeatureSettings.get(RuntimeFeatureSettingType::PatchCableResolution) == RuntimeFeatureStateToggle::Off);
}

void PatchCableStrength::renderOLED() {

	int32_t extraY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 0 : 1;

	PatchSource s = getS();

	int32_t yTop = extraY + OLED_MAIN_TOPMOST_PIXEL;
	int32_t ySpacing;

	ParamDescriptor destinationDescriptor = getDestinationDescriptor();
	if (destinationDescriptor.isJustAParam()) {
		yTop += 3;
		ySpacing = kTextSpacingY;
	}
	else {
		yTop++;
		ySpacing = 8;
	}

	int32_t yPixel = yTop;

	deluge::hid::display::OLED::drawString(getSourceDisplayNameForOLED(s), 0, yPixel,
	                                       deluge::hid::display::OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
	                                       kTextSpacingX, kTextSizeYUpdated);
	yPixel += ySpacing;

	if (!destinationDescriptor.isJustAParam()) {
		// deluge::hid::display::OLED::drawGraphicMultiLine(deluge::hid::display::OLED::downArrowIcon, 0, yPixel, 8,
		// deluge::hid::display::OLED::oledMainImage[0]);
		int32_t horizontalLineY = yPixel + (ySpacing << 1);
		deluge::hid::display::OLED::drawVerticalLine(4, yPixel + 1, horizontalLineY,
		                                             deluge::hid::display::OLED::oledMainImage);
		int32_t rightArrowX = 3 + kTextSpacingX;
		deluge::hid::display::OLED::drawHorizontalLine(horizontalLineY, 4, kTextSpacingX * 2 + 4,
		                                               deluge::hid::display::OLED::oledMainImage);
		deluge::hid::display::OLED::drawGraphicMultiLine(deluge::hid::display::OLED::rightArrowIcon, rightArrowX,
		                                                 horizontalLineY - 2, 3,
		                                                 deluge::hid::display::OLED::oledMainImage[0]);

		yPixel += ySpacing - 1;

		PatchSource s2 = destinationDescriptor.getTopLevelSource();
		deluge::hid::display::OLED::drawString(getSourceDisplayNameForOLED(s2), kTextSpacingX * 2, yPixel - 3,
		                                       deluge::hid::display::OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
		                                       kTextSpacingX, kTextSizeYUpdated);
		yPixel += ySpacing;

		deluge::hid::display::OLED::drawVerticalLine(kTextSpacingX * 2 + 4, yPixel - 2, yPixel + 2,
		                                             deluge::hid::display::OLED::oledMainImage);
	}

	deluge::hid::display::OLED::drawGraphicMultiLine(deluge::hid::display::OLED::downArrowIcon,
	                                                 destinationDescriptor.isJustAParam() ? 2 : (kTextSpacingX * 2 + 2),
	                                                 yPixel, 5, deluge::hid::display::OLED::oledMainImage[0]);
	yPixel += ySpacing;

	int32_t p = destinationDescriptor.getJustTheParam();

	deluge::hid::display::OLED::drawString(deluge::modulation::params::getPatchedParamDisplayName(p), 0, yPixel,
	                                       deluge::hid::display::OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
	                                       kTextSpacingX, kTextSizeYUpdated);

	if (soundEditor.numberEditPos != getDefaultEditPos()) {
		preferBarDrawing = false;
	}

	char buffer[12];
	if (preferBarDrawing) {
		int32_t rounded = this->getValue() / 100;
		intToString(rounded, buffer, 1);
		deluge::hid::display::OLED::drawStringAlignRight(
		    buffer, extraY + OLED_MAIN_TOPMOST_PIXEL + 4 + destinationDescriptor.isJustAParam(),
		    deluge::hid::display::OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, 18, 20);

		int32_t marginL = destinationDescriptor.isJustAParam() ? 0 : 80;
		int32_t yBar = destinationDescriptor.isJustAParam() ? 36 : 37;
		drawBar(yBar, marginL, 0);
	}
	else {
		const int32_t digitWidth = kTextBigSpacingX;
		const int32_t digitHeight = kTextBigSizeY;
		intToString(this->getValue(), buffer, 3);
		int32_t textPixelY = extraY + OLED_MAIN_TOPMOST_PIXEL + 10 + destinationDescriptor.isJustAParam();
		deluge::hid::display::OLED::drawStringAlignRight(buffer, textPixelY,
		                                                 deluge::hid::display::OLED::oledMainImage[0],
		                                                 OLED_MAIN_WIDTH_PIXELS, digitWidth, digitHeight);

		int32_t ourDigitStartX = OLED_MAIN_WIDTH_PIXELS - (soundEditor.numberEditPos + 1) * digitWidth;
		deluge::hid::display::OLED::setupBlink(ourDigitStartX, digitWidth, 40, 44, movingCursor);
		deluge::hid::display::OLED::drawVerticalLine(OLED_MAIN_WIDTH_PIXELS - 2 * digitWidth,
		                                             textPixelY + digitHeight + 1, textPixelY + digitHeight + 3,
		                                             deluge::hid::display::OLED::oledMainImage);
		deluge::hid::display::OLED::drawVerticalLine(OLED_MAIN_WIDTH_PIXELS - 2 * digitWidth - 1,
		                                             textPixelY + digitHeight + 1, textPixelY + digitHeight + 3,
		                                             deluge::hid::display::OLED::oledMainImage);
	}
}

void PatchCableStrength::readCurrentValue() {
	PatchCableSet* patchCableSet = soundEditor.currentParamManager->getPatchCableSet();
	uint32_t c = patchCableSet->getPatchCableIndex(getS(), getDestinationDescriptor());
	if (c == 255) {
		this->setValue(0);
	}
	else {
		int32_t paramValue = patchCableSet->patchCables[c].param.getCurrentValue();
		// the internal values are stored in the range -(2^30) to 2^30.
		// rescale them to the range -5000 to 5000 and round to nearest.
		this->setValue(((int64_t)paramValue * kMaxMenuPatchCableValue + (1 << 29)) >> 30);
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

	// rescale from 5000 to 2^30. The magic constant is ((2^30)/5000), shifted 32 bits for precision ((1<<(30+32))/5000)
	int64_t magicConstant = (922337203685477 * 5000) / kMaxMenuPatchCableValue;
	int32_t finalValue = (magicConstant * this->getValue()) >> 32;
	modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(finalValue, modelStackWithParam);
}

MenuPermission PatchCableStrength::checkPermissionToBeginSession(Sound* sound, int32_t whichThing,
                                                                 MultiRange** currentRange) {

	ParamDescriptor destinationDescriptor = getDestinationDescriptor();
	PatchSource s = getS();

	// If patching to another cable's range...
	if (!destinationDescriptor.isJustAParam()) {
		// Global source - can control any range
		if (s < kFirstLocalSource) {
			return MenuPermission::YES;
		}

		// Local source - range must be for cable going to local param
		return (destinationDescriptor.getJustTheParam() < deluge::modulation::params::FIRST_GLOBAL)
		           ? MenuPermission::YES
		           : MenuPermission::NO;
	}

	int32_t p = destinationDescriptor.getJustTheParam();

	// Note, that requires soundEditor.currentParamManager be set before this is called, which isn't quite ideal.
	if (sound->maySourcePatchToParam(s, p, ((ParamManagerForTimeline*)soundEditor.currentParamManager))
	    == PatchCableAcceptance::DISALLOWED) {
		return MenuPermission::NO;
	}

	return MenuPermission::YES;
}

uint8_t PatchCableStrength::getIndexOfPatchedParamToBlink() {
	if (soundEditor.patchingParamSelected == deluge::modulation::params::GLOBAL_VOLUME_POST_REVERB_SEND
	    || soundEditor.patchingParamSelected == deluge::modulation::params::LOCAL_VOLUME) {
		return deluge::modulation::params::GLOBAL_VOLUME_POST_FX;
	}
	return soundEditor.patchingParamSelected;
}

MenuItem* PatchCableStrength::selectButtonPress() {

	// If shift held down, delete automation
	if (Buttons::isShiftButtonPressed()) {
		Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, ActionAddition::NOT_ALLOWED);

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* modelStack = getModelStack(modelStackMemory);
		if (modelStack->autoParam) {
			modelStack->autoParam->deleteAutomation(action, modelStack);
		}

		display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_DELETED));
		return (MenuItem*)0xFFFFFFFF; // No navigation
	}
	return nullptr; // Navigate back
}
} // namespace deluge::gui::menu_item
