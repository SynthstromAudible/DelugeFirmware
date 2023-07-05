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
#include "processing/engines/cv_engine.h"
#include "gui/menu_item/decimal.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"

namespace menu_item::cv {
class Volts final : public Decimal {
public:
	using Decimal::Decimal;
	int getMinValue() const { return 0; }
	int getMaxValue() const { return 200; }
	int getNumDecimalPlaces() const { return 2; }
	int getDefaultEditPos() { return 1; }
	void readCurrentValue() {
		soundEditor.currentValue = cvEngine.cvChannels[soundEditor.currentSourceIndex].voltsPerOctave;
	}
	void writeCurrentValue() { cvEngine.setCVVoltsPerOctave(soundEditor.currentSourceIndex, soundEditor.currentValue); }
#if HAVE_OLED
	void drawPixelsForOled() {
		if (soundEditor.currentValue == 0) {
			OLED::drawStringCentred("Hz/V", 20, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_HUGE_SPACING_X,
			                        TEXT_HUGE_SIZE_Y);
		}
		else {
			Decimal::drawPixelsForOled();
		}
	}
#else
	void drawValue() {
		if (soundEditor.currentValue == 0)
			numericDriver.setText("HZPV", false, 255, true);
		else {
			Decimal::drawValue();
		}
	}
#endif
	void horizontalEncoderAction(int offset) {
		if (soundEditor.currentValue != 0) {
			Decimal::horizontalEncoderAction(offset);
		}
	}
};
} // namespace menu_item::cv
