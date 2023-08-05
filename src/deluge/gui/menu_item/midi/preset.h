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
#include "hid/display/numeric_driver.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/output.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::midi {
class Preset : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMaxValue() const override { return 128; } // Probably not needed cos we override below...

	void drawInteger(int32_t textWidth, int32_t textHeight, int32_t yPixel) {
		char buffer[12];
		char const* text;
		if (this->value_ == 128) {
			text = "NONE";
		}
		else {
			intToString(this->value_ + 1, buffer, 1);
			text = buffer;
		}
		OLED::drawStringCentred(text, yPixel + OLED_MAIN_TOPMOST_PIXEL, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
		                        textWidth, textHeight);
	}

	void drawValue() override {
		if (this->value_ == 128) {
			display.setText("NONE");
		}
		else {
			display.setTextAsNumber(this->value_ + 1);
		}
	}

	bool isRelevant(Sound* sound, int32_t whichThing) override {
		return currentSong->currentClip->output->type == InstrumentType::MIDI_OUT;
	}

	void selectEncoderAction(int32_t offset) override {
		this->value_ += offset;
		if (this->value_ >= 129) {
			this->value_ -= 129;
		}
		else if (this->value_ < 0) {
			this->value_ += 129;
		}
		Number::selectEncoderAction(offset);
	}
};
} // namespace deluge::gui::menu_item::midi
