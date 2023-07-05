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
#pragma once
#include "gui/menu_item/decimal.h"
#include "processing/sound/sound.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"

namespace menu_item::osc {
class RetriggerPhase final : public Decimal {
public:
	RetriggerPhase(char const* newName = NULL, bool newForModulator = false) : Decimal(newName) {
		forModulator = newForModulator;
	}
	int getMinValue() const { return -soundEditor.numberEditSize; }
	int getMaxValue() const { return 360; }
	int getNumDecimalPlaces() const { return 0; }
	int getDefaultEditPos() const { return 1; }
	void readCurrentValue() {
		uint32_t value = *getValueAddress();
		if (value == 0xFFFFFFFF) {
			soundEditor.currentValue = -soundEditor.numberEditSize;
		}
		else {
			soundEditor.currentValue = value / 11930464;
		}
	}
	void writeCurrentValue() {
		uint32_t value;
		if (soundEditor.currentValue < 0) {
			value = 0xFFFFFFFF;
		}
		else {
			value = soundEditor.currentValue * 11930464;
		}
		*getValueAddress() = value;
	}
	void drawValue() {
		if (soundEditor.currentValue < 0) {
			numericDriver.setText("OFF", false, 255, true);
		}
		else {
			Decimal::drawValue();
		}
	}
#if HAVE_OLED
	void drawPixelsForOled() {
		if (soundEditor.currentValue < 0) {
			OLED::drawStringCentred("OFF", 20, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_HUGE_SPACING_X,
			                        TEXT_HUGE_SIZE_Y);
		}
		else {
			Decimal::drawPixelsForOled();
		}
	}
#endif
	void horizontalEncoderAction(int offset) {
		if (soundEditor.currentValue >= 0) {
			Decimal::horizontalEncoderAction(offset);
		}
	}

	bool isRelevant(Sound* sound, int whichThing) {
		Source* source = &sound->sources[whichThing];
		if (forModulator && sound->getSynthMode() != SYNTH_MODE_FM) {
			return false;
		}
		return (source->oscType != OSC_TYPE_SAMPLE || sound->getSynthMode() == SYNTH_MODE_FM);
	}

private:
	bool forModulator;
	uint32_t* getValueAddress() {
		if (forModulator) {
			return &soundEditor.currentSound->modulatorRetriggerPhase[soundEditor.currentSourceIndex];
		}
		else {
			return &soundEditor.currentSound->oscRetriggerPhase[soundEditor.currentSourceIndex];
		}
	}
};
} // namespace menu_item::osc
