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
#include "gui/menu_item/formatted_title.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "processing/engines/cv_engine.h"

namespace deluge::gui::menu_item::cv {
class Volts final : public Decimal, public FormattedTitle {
public:
	Volts(l10n::String name, l10n::String title_format_str) : Decimal(name), FormattedTitle(title_format_str) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 200; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 2; }
	[[nodiscard]] int32_t getDefaultEditPos() const override { return 1; }
	void readCurrentValue() override {
		this->setValue(cvEngine.cvChannels[soundEditor.currentSourceIndex].voltsPerOctave);
	}
	void writeCurrentValue() override {
		cvEngine.setCVVoltsPerOctave(soundEditor.currentSourceIndex, this->getValue());
	}
	void drawPixelsForOled() override {
		deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;
		if (this->getValue() == 0) {
			canvas.drawStringCentred("Hz/V", 20, kTextHugeSpacingX, kTextHugeSizeY);
		}
		else {
			Decimal::drawPixelsForOled();
		}
	}

	void drawValue() override {
		if (this->getValue() == 0) {
			display->setText("HZPV", false, 255, true);
		}
		else {
			Decimal::drawValue();
		}
	}

	void horizontalEncoderAction(int32_t offset) override {
		if (this->getValue() != 0) {
			Decimal::horizontalEncoderAction(offset);
		}
	}
};
} // namespace deluge::gui::menu_item::cv
