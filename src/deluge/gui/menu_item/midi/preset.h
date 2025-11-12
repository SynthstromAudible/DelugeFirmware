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
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/clip/instrument_clip.h"
#include "model/output.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::midi {
class Preset : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMaxValue() const override { return 128; } // Probably not needed cos we override below...

	void drawInteger(int32_t textWidth, int32_t textHeight, int32_t yPixel) override {
		oled_canvas::Canvas& canvas = OLED::main;
		char buffer[12];
		char const* text;
		if (this->getValue() == 128) {
			text = l10n::get(l10n::String::STRING_FOR_NONE);
		}
		else {
			intToString(this->getValue(), buffer, 1);
			text = buffer;
		}
		canvas.drawStringCentred(text, yPixel + OLED_MAIN_TOPMOST_PIXEL, textWidth, textHeight);
	}

	void drawValue() override {
		if (this->getValue() == 128) {
			display->setText(l10n::get(l10n::String::STRING_FOR_NONE));
		}
		else {
			display->setTextAsNumber(this->getValue());
		}
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return getCurrentOutputType() == OutputType::MIDI_OUT;
	}

	void selectEncoderAction(int32_t offset) override {
		this->setValue(this->getValue() + offset);
		if (this->getValue() >= 129) {
			this->setValue(this->getValue() - 129);
		}
		else if (this->getValue() < 0) {
			this->setValue(this->getValue() + 129);
		}
		Number::selectEncoderAction(offset);
	}

	void renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) override {
		oled_canvas::Canvas& image = OLED::main;

		DEF_STACK_STRING_BUF(paramValue, 5);
		int32_t size_x, size_y;
		if (this->getValue() == 128) {
			paramValue.append(l10n::get(l10n::String::STRING_FOR_NONE));
			size_x = kTextSpacingX;
			size_y = kTextSpacingY;
		}
		else {
			paramValue.appendInt(getValue());
			size_x = kTextTitleSpacingX;
			size_y = kTextTitleSizeY;
		}
		image.drawStringCentered(paramValue, slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset, size_x, size_y,
		                         slot.width);
	}

	void configureRenderingOptions(const HorizontalMenuRenderingOptions &options) override {
		options.show_notification = false;
	}
};
} // namespace deluge::gui::menu_item::midi
