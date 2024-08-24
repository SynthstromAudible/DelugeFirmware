/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "integer.h"
#include "gui/menu_item/value_scaling.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "hid/display/oled.h"
#include "model/clip/clip.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"

namespace deluge::gui::menu_item::patched_param {
void Integer::readCurrentValue() {
	this->setValue(computeCurrentValueForStandardMenuItem(
	    soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP())));
}

void Integer::writeCurrentValue() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStack = getModelStack(modelStackMemory);
	int32_t value = getFinalValue();
	modelStack->autoParam->setCurrentValueInResponseToUserInput(value, modelStack);

	// send midi follow feedback
	int32_t knobPos = modelStack->paramCollection->paramValueToKnobPos(value, modelStack);
	view.sendMidiFollowFeedback(modelStack, knobPos);

	if (getRootUI() == &automationView) {
		int32_t p = modelStack->paramId;
		modulation::params::Kind kind = modelStack->paramCollection->getParamKind();
		automationView.possiblyRefreshAutomationEditorGrid(getCurrentClip(), kind, p);
	}

	//((ParamManagerBase*)soundEditor.currentParamManager)->setPatchedParamValue(getP(), getFinalValue(), 0xFFFFFFFF, 0,
	// soundEditor.currentSound, currentSong, getCurrentClip(), true, true);
}

int32_t Integer::getFinalValue() {
	return computeFinalValueForStandardMenuItem(this->getValue());
}

// TODO: This is identical with the unpatched integer version. Can we share the code? How about
// having a Integer class which both PatchedInteger and UnpatchedInteger would inherit from?
void Integer::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
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
	paramValue.appendInt(getValue());

	int32_t pxLen = image.getStringWidthInPixels(paramValue.c_str(), kTextTitleSizeY);
	int32_t pad = (width + 1 - pxLen) / 2;
	image.drawString(paramValue.c_str(), startX + pad, startY + kTextSpacingY + 2, kTextTitleSpacingX, kTextTitleSizeY,
	                 0, startX + width);
}

} // namespace deluge::gui::menu_item::patched_param
