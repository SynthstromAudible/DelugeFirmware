/*
 * Copyright (c) 2025 Synthstrom Audible Limited
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
#include "rate.h"
#include "deluge.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::stutter {

std::vector<const char*> Rate::optionLabels{"1 BAR", "2nds", "4ths", "8ths", "16ths", "32nds"};
std::vector<int32_t> Rate::optionValues{2, 6, 13, 19, 25, 31};

void Rate::selectEncoderAction(int32_t offset) {
	if (!isStutterQuantized()) {
		return UnpatchedParam::selectEncoderAction(offset);
	}

	const int32_t value = getValue(); // 0-50
	int32_t idx = getClosestQuantizedOptionIndex(value);
	idx = std::clamp<int32_t>(idx + offset, 0, static_cast<int32_t>(optionLabels.size()) - 1);

	setValue(optionValues[idx]);
	writeCurrentValue();
	drawValue();
}

// For 7SEG display
void Rate::drawValue() {
	if (!isStutterQuantized()) {
		return UnpatchedParam::drawValue();
	}

	const char* label = getQuantizedOptionLabel();
	display->setText(label);
}

void Rate::drawPixelsForOled() {
	if (!isStutterQuantized()) {
		return UnpatchedParam::drawValue();
	}

	const char* label = getQuantizedOptionLabel();
	deluge::hid::display::OLED::main.drawStringCentred(label, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
	                                                   kTextHugeSizeY);
}

void Rate::renderInHorizontalMenu(const SlotPosition& slot) {
	if (!isStutterQuantized()) {
		return UnpatchedParam::renderInHorizontalMenu(slot);
	}

	const char* label = getQuantizedOptionLabel();
	hid::display::OLED::main.drawStringCentered(label, slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset,
	                                            kTextSpacingX, kTextSpacingY, slot.width);
}

void Rate::getNotificationValue(StringBuf& valueBuf) {
	if (!isStutterQuantized()) {
		return valueBuf.appendInt(getValue());
	}
	valueBuf.append(getQuantizedOptionLabel());
}

bool Rate::isStutterQuantized() {
	if (soundEditor.currentModControllable->stutterConfig.useSongStutter) {
		return currentSong->globalEffectable.stutterConfig.quantized;
	}

	return soundEditor.currentModControllable->stutterConfig.quantized;
}

const char* Rate::getQuantizedOptionLabel() {
	const int32_t value = getValue(); // 0-50
	const int32_t idx = getClosestQuantizedOptionIndex(value);
	return optionLabels[idx];
}

int32_t Rate::getClosestQuantizedOptionIndex(int32_t value) const {
	int32_t idx = 0;
	int32_t minDiff = std::abs(value - optionValues[0]);
	for (int i = 1; i < static_cast<int32_t>(optionValues.size()); ++i) {
		int32_t diff = std::abs(value - optionValues[i]);
		if (diff < minDiff) {
			minDiff = diff;
			idx = i;
		}
	}
	return idx;
}

} // namespace deluge::gui::menu_item::stutter
