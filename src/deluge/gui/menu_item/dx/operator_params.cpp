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

#include "operator_params.h"
#include "dsp/dx/engine.h"
#include "gui/menu_item/dx/param.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "memory/general_memory_allocator.h"
#include "processing/source.h"
#include <etl/vector.h>

namespace deluge::gui::menu_item {

DxOperatorParams dxOperatorParams{l10n::String::STRING_FOR_DX_GLOBAL_PARAMS,
                                  l10n::String::STRING_FOR_DX_OPERATOR_PARAMS};

void DxOperatorParams::beginSession(MenuItem* navigatedBackwardFrom) {
	readValueAgain();
}

void DxOperatorParams::readValueAgain() {
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
    {"pitch", "tune", 18},
    {"level", "lvl", 16},
    {"envelope", "env", 0},
    {"velocity sense", "velo sens", 15},
    {"note scaling", "note scal", 8},
    {"rate scaling", "rate scal", 13},
    {"ampmod", "ampmod", 14},
};

constexpr int numValues = sizeof(items) / sizeof(items[0]);

void DxOperatorParams::drawPixelsForOled() {
	etl::vector<std::string_view, numValues> itemNames = {};
	for (auto& item : items) {
		itemNames.push_back(item.name);
	}
	drawItemsForOled(itemNames, currentValue - scrollPos, scrollPos);
}

void DxOperatorParams::drawValue() {
	display->setScrollingText(items[currentValue].name);
}

void DxOperatorParams::selectEncoderAction(int32_t offset) {
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

MenuItem* DxOperatorParams::selectButtonPress() {
	int index = items[currentValue].index;
	dxParam.param = op * 21 + index;
	return &dxParam;
}

} // namespace deluge::gui::menu_item
