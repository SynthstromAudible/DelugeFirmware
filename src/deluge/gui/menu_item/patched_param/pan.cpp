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
#include "pan.h"

#include "gui/menu_item/value_scaling.h"
#include "gui/ui/sound_editor.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "util/cfunctions.h"
#include <cmath>
#include <cstring>

namespace deluge::gui::menu_item::patched_param {
// 7SEG only
void Pan::drawValue() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(getP());
	uint8_t drawDot =
	    soundEditor.currentParamManager->getPatchCableSet()->isAnySourcePatchedToParamVolumeInspecific(paramDescriptor)
	        ? 3
	        : 255;
	char buffer[5];
	intToString(std::abs(this->getValue()), buffer, 1);
	if (this->getValue() < 0) {
		strcat(buffer, "L");
	}
	else if (this->getValue() > 0) {
		strcat(buffer, "R");
	}
	display->setText(buffer, true, drawDot);
}

int32_t Pan::getFinalValue() {
	return computeFinalValueForPan(this->getValue());
}

void Pan::readCurrentValue() {
	this->setValue(computeCurrentValueForPan(soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP())));
}
} // namespace deluge::gui::menu_item::patched_param
