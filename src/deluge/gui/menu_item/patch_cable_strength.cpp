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
#include "definitions_cxx.hpp"
#include "gui/menu_item/menu_item.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "horizontal_menu.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param_descriptor.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "source_selection.h"
#include "source_selection/range.h"
#include "util/functions.h"

using hid::display::OLED;

namespace deluge::gui::menu_item {
extern bool movingCursor;

void PatchCableStrength::beginSession(MenuItem* navigatedBackwardFrom) {
	Decimal::beginSession(navigatedBackwardFrom);

	delayHorizontalScrollUntil = 0;

	if (isInHorizontalMenu()) {
		return;
	}

	auto* patch_cable_set = soundEditor.currentParamManager->getPatchCableSet();
	const uint32_t patch_cable_index = patch_cable_set->getPatchCableIndex(getS(), getDestinationDescriptor());

	if (patch_cable_index == kNoSelection) {
		patch_cable_exists_ = false;
		polarity_in_the_ui_ = PatchCable::getDefaultPolarity(getS());
	}
	else {
		patch_cable_exists_ = true;
		polarity_in_the_ui_ = patch_cable_set->patchCables[patch_cable_index].polarity;
	}
	setPatchCablePolarity(polarity_in_the_ui_);
	updatePolarityUI();
}

void PatchCableStrength::endSession() {
	if (display->haveOLED()) {
		setLedState(IndicatorLED::MIDI, false);
		setLedState(IndicatorLED::CV, false);
	}
}

void PatchCableStrength::renderOLED() {
	hid::display::oled_canvas::Canvas& image = OLED::main;

	constexpr int32_t extraY = 1;

	// Draw patch sources and destination
	{
		const ParamDescriptor destinationDescriptor = getDestinationDescriptor();
		const PatchSource source = getS();
		const bool isJustAParam = destinationDescriptor.isJustAParam();

		constexpr int32_t leftPadding = 2;
		const int32_t ySpacing = isJustAParam ? kTextSpacingY : 8;
		int32_t y = extraY + OLED_MAIN_TOPMOST_PIXEL;
		y += isJustAParam ? 4 : 1;

		// Draw the source name
		image.drawString(sourceToStringShort(source), leftPadding, y, kTextSpacingX, kTextSizeYUpdated);
		y += ySpacing;

		if (!isJustAParam) {
			// Draw arrow line to the second source
			const int32_t horizontalLineY = y + (ySpacing << 1);
			image.drawVerticalLine(leftPadding + 4, y + 1, horizontalLineY);
			image.drawHorizontalLine(horizontalLineY, leftPadding + 4, leftPadding + kTextSpacingX * 2 + 4);
			image.drawGraphicMultiLine(OLED::rightArrowIcon, leftPadding + 3 + kTextSpacingX, horizontalLineY - 2, 3);
			y += ySpacing - 1;

			// Draw the second source name
			image.drawString(sourceToStringShort(destinationDescriptor.getTopLevelSource()),
			                 leftPadding + kTextSpacingX * 2, y - 3, kTextSpacingX, kTextSizeYUpdated);
			y += ySpacing;

			// Draw arrow line to the destination
			image.drawVerticalLine(leftPadding + kTextSpacingX * 2 + 4, y - 2, y + 2);
			image.drawGraphicMultiLine(OLED::downArrowIcon, leftPadding + kTextSpacingX * 2 + 2, y, 5);
			y += ySpacing;
		}
		else {
			// Draw arrow line to the destination
			image.drawVerticalLine(leftPadding + 3, y + 2, y + 5);
			image.drawGraphicMultiLine(OLED::downArrowIcon, leftPadding + 1, y + 6, 5);
			y += 16;
		}

		// Draw the destination name
		image.drawString(modulation::params::getPatchedParamShortName(destinationDescriptor.getJustTheParam()),
		                 leftPadding, y, kTextSpacingX, kTextSizeYUpdated);
	}

	// Draw the polarity switcher
	{
		constexpr int32_t startX = 73;
		constexpr int32_t biSlotWidth = 25;
		constexpr int32_t uniSlotWidth = 28;
		constexpr int32_t endX = startX + biSlotWidth + uniSlotWidth - 1;
		constexpr int32_t startY = 34;
		constexpr int32_t endY = startY + kTextSpacingY;

		// Draw border
		image.drawVerticalLine(startX - 2, startY, endY);
		image.drawVerticalLine(endX + 2, startY, endY);
		image.drawHorizontalLine(startY - 2, startX, endX);
		image.drawPixel(startX - 1, startY - 1);
		image.drawPixel(endX + 1, startY - 1);

		if (PatchCable::hasPolarity(getS())) {
			// Draw BI / UNI strings
			image.drawStringCentered("BI", startX, startY, kTextSpacingX, kTextSpacingY, biSlotWidth);
			image.drawStringCentered("UNI", startX + biSlotWidth + 1, startY, kTextSpacingX, kTextSpacingY,
			                         uniSlotWidth);
			// Highlight selected
			int32_t x = polarity_in_the_ui_ == Polarity::BIPOLAR ? startX : startX + biSlotWidth;
			int32_t width = polarity_in_the_ui_ == Polarity::BIPOLAR ? biSlotWidth : uniSlotWidth;
			image.invertAreaRounded(x, width, startY, endY);
		}
		else {
			// If the param doesn't support polarity, show only default polarity
			const auto defaultPolarity = polarityToString(PatchCable::getDefaultPolarity(getS()));
			image.drawStringCentered(defaultPolarity.data(), startX, startY, kTextSpacingX, kTextSpacingY,
			                         biSlotWidth + uniSlotWidth);
		}
	}

	// Draw the value number
	{
		const int32_t value = getValue();
		const int32_t nonZeroDecimals = getNumNonZeroDecimals(value);

		// We hide the fractional part digit if it's zero and the cursor is not on the fractional part
		const int32_t hiddenZeroesCount = std::clamp<int32_t>(2 - nonZeroDecimals, 0, soundEditor.numberEditPos);
		const int32_t numberEditPos = soundEditor.numberEditPos - hiddenZeroesCount;
		const int32_t numberStr = value / std::pow(10, hiddenZeroesCount);

		char numberBuf[6];
		intToString(numberStr, numberBuf, 3 - hiddenZeroesCount);

		constexpr int32_t digitWidth = kTextBigSpacingX;
		constexpr int32_t digitHeight = kTextBigSizeY;

		constexpr int32_t textY = extraY + OLED_MAIN_TOPMOST_PIXEL + 4;
		constexpr int32_t rightPadding = 3;
		image.drawStringAlignRight(numberBuf, textY, digitWidth, digitHeight, OLED_MAIN_WIDTH_PIXELS - rightPadding);

		// Setup blinking for cursor
		const int32_t cursorStartX = OLED_MAIN_WIDTH_PIXELS - (numberEditPos + 1) * digitWidth - rightPadding;
		OLED::setupBlink(cursorStartX, digitWidth, textY + digitHeight + 1, textY + digitHeight + 1, movingCursor);

		// Draw the cursor
		if (const int32_t fractionalDigitsCount = 2 - hiddenZeroesCount; fractionalDigitsCount > 0) {
			const int32_t separatorX = OLED_MAIN_WIDTH_PIXELS - fractionalDigitsCount * digitWidth - rightPadding;
			image.drawVerticalLine(separatorX, textY + digitHeight + 1, textY + digitHeight + 3);
			image.drawVerticalLine(separatorX - 1, textY + digitHeight + 1, textY + digitHeight + 3);
		}
	}
}

void PatchCableStrength::readCurrentValue() {
	PatchCableSet* patchCableSet = soundEditor.currentParamManager->getPatchCableSet();
	uint32_t c = patchCableSet->getPatchCableIndex(getS(), getDestinationDescriptor());
	if (c == 255) {
		this->setValue(0);
		patch_cable_exists_ = false;
	}
	else {
		PatchCable& patchCable = patchCableSet->patchCables[c];
		const int32_t paramValue = patchCable.param.getCurrentValue();
		// the internal values are stored in the range -(2^30) to 2^30.
		// rescale them to the range -5000 to 5000 and round to nearest.
		this->setValue(((int64_t)paramValue * kMaxMenuPatchCableValue + (1 << 29)) >> 30);
	}
}

ModelStackWithAutoParam* PatchCableStrength::getModelStackWithParam(void* memory) {
	return getModelStack(memory);
}

// Might return a ModelStack with NULL autoParam - check for that!
ModelStackWithAutoParam* PatchCableStrength::getModelStack(void* memory, bool allowCreation) {
	ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
	ParamCollectionSummary* paramSetSummary = modelStack->paramManager->getPatchCableSetSummary();

	ModelStackWithParamCollection* modelStackWithParamCollection =
	    modelStack->addParamCollectionSummary(paramSetSummary);
	ModelStackWithParamId* ModelStackWithParamId = modelStackWithParamCollection->addParamId(getLearningThing().data);
	ModelStackWithAutoParam* modelStackMaybeWithAutoParam =
	    paramSetSummary->paramCollection->getAutoParamFromId(ModelStackWithParamId, allowCreation);

	if (allowCreation && modelStackMaybeWithAutoParam->autoParam && !patch_cable_exists_ && !isInHorizontalMenu()) {
		// If we created a patch cable then set the polarity to match the menus
		setPatchCablePolarity(polarity_in_the_ui_);
		patch_cable_exists_ = true;
	}
	return modelStackMaybeWithAutoParam;
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

	if (getRootUI() == &automationView) {
		int32_t p = modelStackWithParam->paramId;
		modulation::params::Kind kind = modelStackWithParam->paramCollection->getParamKind();
		automationView.possiblyRefreshAutomationEditorGrid(getCurrentClip(), kind, p);
	}
}

MenuPermission PatchCableStrength::checkPermissionToBeginSession(ModControllableAudio* modControllable,
                                                                 int32_t whichThing, MultiRange** currentRange) {

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

	Sound* sound = static_cast<Sound*>(modControllable);

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

deluge::modulation::params::Kind PatchCableStrength::getParamKind() {
	return deluge::modulation::params::Kind::PATCH_CABLE;
}

uint32_t PatchCableStrength::getParamIndex() {
	return getLearningThing().data;
}

PatchSource PatchCableStrength::getPatchSource() {
	return getS();
}

MenuItem* PatchCableStrength::selectButtonPress() {
	// Cancel the polarity popup on the 7SEG
	if (display->have7SEG() && display->hasPopup()) {
		display->cancelPopup();
		return NO_NAVIGATION;
	}
	// If shift held down, delete automation
	if (Buttons::isShiftButtonPressed()) {
		return Automation::selectButtonPress();
	}
	// Go to the range menu or back if we in the range menu already
	return &source_selection::rangeMenu;
}

ActionResult PatchCableStrength::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	static auto polarity_map = std::map<hid::Button, Polarity>{{MIDI, Polarity::BIPOLAR}, {CV, Polarity::UNIPOLAR}};
	if (on && display->haveOLED() && polarity_map.contains(b) && PatchCable::hasPolarity(getS())) {
		polarity_in_the_ui_ = polarity_map[b];
		setPatchCablePolarity(polarity_in_the_ui_);
		updatePolarityUI();
		return ActionResult::DEALT_WITH;
	}

	return Automation::buttonAction(b, on, inCardRoutine);
}

void PatchCableStrength::selectEncoderAction(int32_t offset) {
	if (!isInHorizontalMenu() && Buttons::isButtonPressed(hid::button::SELECT_ENC) && PatchCable::hasPolarity(getS())) {
		polarity_in_the_ui_ = offset > 0 ? Polarity::UNIPOLAR : Polarity::BIPOLAR;
		setPatchCablePolarity(polarity_in_the_ui_);
		updatePolarityUI();

		if (display->haveOLED()) {
			Buttons::selectButtonPressUsedUp = true;
		}
		else {
			display->popupText(polarityToStringShort(polarity_in_the_ui_).data());
		}
		return;
	}

	return Decimal::selectEncoderAction(offset);
}

void PatchCableStrength::horizontalEncoderAction(int32_t offset) {
	int8_t currentEditPos = soundEditor.numberEditPos;
	// don't adjust patch cable decimal edit pos if you're holding down the horizontal encoder
	// reserve holding down horizontal encoder for zooming in automation view
	if (!Buttons::isButtonPressed(hid::button::X_ENC)) {
		Decimal::horizontalEncoderAction(offset);
	}
	// if editPos hasn't changed, then you reached start (far left) or end (far right) of the decimal number
	// or you're holding down the horizontal encoder because you want to zoom in/out
	// if this is the case, then you can potentially engage scrolling/zooming of the underlying automation view
	if (currentEditPos == soundEditor.numberEditPos) {
		if (delayHorizontalScrollUntil == 0) {
			delayHorizontalScrollUntil = AudioEngine::audioSampleTimer + kShortPressTime;
		}
		else if (AudioEngine::audioSampleTimer > delayHorizontalScrollUntil) {
			RootUI* rootUI = getRootUI();
			if (rootUI == &automationView) {
				automationView.horizontalEncoderAction(offset);
			}
			else if (rootUI == &keyboardScreen) {
				keyboardScreen.horizontalEncoderAction(offset);
			}
		}
	}
	else {
		delayHorizontalScrollUntil = 0;
	}
}

bool PatchCableStrength::isInHorizontalMenu() const {
	return parent != nullptr && parent->renderingStyle() == Submenu::RenderingStyle::HORIZONTAL;
}

void PatchCableStrength::setPatchCablePolarity(Polarity newPolarity) {
	if (!PatchCable::hasPolarity(getS())) {
		return;
	}

	auto* patchCableSet = soundEditor.currentParamManager->getPatchCableSet();
	if (const int32_t index = patchCableSet->getPatchCableIndex(getS(), getDestinationDescriptor());
	    index != kNoSelection) {
		patchCableSet->patchCables[index].polarity = newPolarity;
	}
}

void PatchCableStrength::updatePolarityUI() {
	if (display->haveOLED()) {
		const bool hasPolarity = PatchCable::hasPolarity(getS());
		setLedState(IndicatorLED::MIDI, hasPolarity && polarity_in_the_ui_ == Polarity::BIPOLAR);
		setLedState(IndicatorLED::CV, hasPolarity && polarity_in_the_ui_ == Polarity::UNIPOLAR);
		renderUIsForOled();
	}
	else {
		// update additional dot on 7seg
		drawActualValue();
	}
}

void PatchCableStrength::appendAdditionalDots(std::vector<uint8_t>& dotPositions) {
	if (polarity_in_the_ui_ == Polarity::UNIPOLAR) {
		dotPositions.push_back(3);
	}
}

} // namespace deluge::gui::menu_item
