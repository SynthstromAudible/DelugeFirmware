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

#include "unpatched_param.h"
#include "gui/menu_item/value_scaling.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "hid/display/oled.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"

namespace deluge::gui::menu_item {

void UnpatchedParam::readCurrentValue() {
	this->setValue(computeCurrentValueForStandardMenuItem(
	    soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP())));
}

ModelStackWithAutoParam* UnpatchedParam::getModelStack(void* memory) {
	ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
	return modelStack->getUnpatchedAutoParamFromId(getP());
}

void UnpatchedParam::writeCurrentValue() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStackWithParam = getModelStack(modelStackMemory);
	int32_t value = getFinalValue();
	modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(value, modelStackWithParam);

	// send midi follow feedback
	int32_t knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(value, modelStackWithParam);
	view.sendMidiFollowFeedback(modelStackWithParam, knobPos);

	if (getRootUI() == &automationView) {
		int32_t p = modelStackWithParam->paramId;
		modulation::params::Kind kind = modelStackWithParam->paramCollection->getParamKind();
		automationView.possiblyRefreshAutomationEditorGrid(getCurrentClip(), kind, p);
	}
}

int32_t UnpatchedParam::getFinalValue() {
	return computeFinalValueForStandardMenuItem(this->getValue());
}

ParamDescriptor UnpatchedParam::getLearningThing() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(getP() + deluge::modulation::params::UNPATCHED_START);
	return paramDescriptor;
}

ParamSet* UnpatchedParam::getParamSet() {
	return soundEditor.currentParamManager->getUnpatchedParamSet();
}

deluge::modulation::params::Kind UnpatchedParam::getParamKind() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	return getModelStack(modelStackMemory)->paramCollection->getParamKind();
}

uint32_t UnpatchedParam::getParamIndex() {
	return this->getP();
}

void UnpatchedParam::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	std::string_view name = getName();
	size_t len = std::min((size_t)(width / kTextSpacingX), name.size());
	// If we can fit the whole name, we do, if we can't we chop one letter off. It just looks and
	// feels better, at least with the names we have now.
	if (name.size() > len) {
		len -= 1;
	}
	std::string_view shortName(name.data(), len);
	image.drawString(shortName, startX, startY, kTextSpacingX, kTextSpacingY, 0, startX + width);

	DEF_STACK_STRING_BUF(paramValue, 10);
	paramValue.appendInt(getParamValue());

	int32_t pxLen = image.getStringWidthInPixels(paramValue.c_str(), kTextTitleSizeY);
	int32_t pad = (width - pxLen) / 2;
	image.drawString(paramValue.c_str(), startX + pad, startY + kTextSpacingY + 2, kTextTitleSpacingX, kTextTitleSizeY,
	                 0, startX + width);
}

// ---------------------------------------

// ---------------------------------------

} // namespace deluge::gui::menu_item
