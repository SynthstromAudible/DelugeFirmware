/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "global_params.h"
#include "dsp/dx/engine.h"
#include "gui/menu_item/dx/operator_params.h"
#include "gui/menu_item/dx/param.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "memory/general_memory_allocator.h"
#include "processing/source.h"

#include <etl/vector.h>

namespace deluge::gui::menu_item {

DxGlobalParams dxGlobalParams{l10n::String::STRING_FOR_DX_GLOBAL_PARAMS};

void DxGlobalParams::beginSession(MenuItem* navigatedBackwardFrom) {
	readValueAgain();
}

void DxGlobalParams::readValueAgain() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}
}

struct {
	const char* name;
	const char* shortname;
	int index;
} items[] = {
    {"algorithm", "algo", 134},         {"feedback", "fbck", 135},  {"sync", "sync", 136},
    {"operator 1", "op 1", -1},         {"operator 2", "op 2", -2}, {"operator 3", "op 3", -3},
    {"operator 4", "op 4", -4},         {"operator 5", "op 5", -5}, {"operator 6", "op 6", -6},
    {"pitch envelope", "penv", 6 * 21}, {"lfo", "lfo", 137},
};

constexpr int numValues = sizeof(items) / sizeof(items[0]);

void DxGlobalParams::drawPixelsForOled() {
	etl::vector<std::string_view, numValues> itemNames = {};
	for (auto& item : items) {
		itemNames.push_back(item.name);
	}
	drawItemsForOled(itemNames, currentValue - scrollPos, scrollPos);
}

void DxGlobalParams::drawValue() {
	display->setScrollingText(items[currentValue].shortname);
}

void DxGlobalParams::selectEncoderAction(int32_t offset) {
	int32_t newValue = currentValue + offset;

	if (display->haveOLED()) {
		if (newValue >= numValues || newValue < 0) {
			return;
		}
	}
	else {
		if (newValue >= numValues) {
			newValue %= numValues;
		}
		else if (newValue < 0) {
			newValue = (newValue % numValues + numValues) % numValues;
		}
	}

	currentValue = newValue;

	if (display->haveOLED()) {
		if (currentValue < scrollPos) {
			scrollPos = currentValue;
		}
		else if (currentValue >= scrollPos + kOLEDMenuNumOptionsVisible) {
			scrollPos++;
		}
	}

	readValueAgain();
}

MenuItem* DxGlobalParams::selectButtonPress() {
	int index = items[currentValue].index;
	if (index >= 0) {
		dxParam.param = index;
		return &dxParam;
	}
	else if (index >= -6) {
		dxOperatorParams.op = 6 + index;
		dxOperatorParams.format(-index);
		return &dxOperatorParams;
	}
	return nullptr;
}

} // namespace deluge::gui::menu_item
