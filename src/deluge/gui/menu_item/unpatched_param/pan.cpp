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

#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include <cmath>
#include <cstring>

extern "C" {
#include "util/cfunctions.h"
}

namespace deluge::gui::menu_item::unpatched_param {

void Pan::drawValue() {    // TODO: should really combine this with the "patched" version
	uint8_t drawDot = 255; //soundEditor.doesParamHaveAnyCables(getP()) ? 3 : 255;
	char buffer[5];
	intToString(std::abs(this->get_value()), buffer, 1);
	if (this->get_value() < 0) {
		strcat(buffer, "L");
	}
	else if (this->get_value() > 0) {
		strcat(buffer, "R");
	}
	numericDriver.setText(buffer, true, drawDot);
}

int32_t Pan::getFinalValue() {
	if (this->get_value() == 32) {
		return 2147483647;
	}
	else if (this->get_value() == -32) {
		return -2147483648;
	}
	else {
		return ((int32_t)this->get_value() * 33554432 * 2);
	}
}

void Pan::readCurrentValue() {
	this->set_value(
	    ((int64_t)soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP()) * 64 + 2147483648) >> 32);
}

} // namespace deluge::gui::menu_item::unpatched_param
