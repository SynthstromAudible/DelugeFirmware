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

#include "engine_select.h"
#include "dsp/dx/engine.h"
#include "gui/menu_item/dx/param.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "memory/general_memory_allocator.h"
#include "processing/source.h"

#include <etl/vector.h>

namespace deluge::gui::menu_item {

DxEngineSelect dxEngineSelect{l10n::String::STRING_FOR_DX_ENGINE_SELECT};

void DxEngineSelect::beginSession(MenuItem* navigatedBackwardFrom) {
	readValueAgain();
}

void DxEngineSelect::readValueAgain() {
	patch = soundEditor.currentSource->ensureDxPatch();
	currentValue = patch->engineMode;
	drawValue();
}

constexpr int numValues = 3;

void DxEngineSelect::drawPixelsForOled() {
	etl::vector<std::string_view, numValues> itemNames = {"auto", "modern", "vintage"};
	drawItemsForOled(itemNames, currentValue, 0);
}

void DxEngineSelect::drawValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		static const char*(items[]) = {"AUTO", "MODR", "VINT"};
		display->setScrollingText(items[currentValue]);
	}
}

void DxEngineSelect::selectEncoderAction(int32_t offset) {
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

	patch->setEngineMode(newValue);
	currentValue = newValue;
	drawValue();
}

MenuItem* DxEngineSelect::selectButtonPress() {
	return nullptr;
}

} // namespace deluge::gui::menu_item
