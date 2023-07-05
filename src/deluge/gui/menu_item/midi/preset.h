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
#include "model/clip/instrument_clip.h"
#include "gui/menu_item/integer.h"
#include "model/clip/clip.h"
#include "model/output.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"
#include "model/song/song.h"
#include "hid/display/oled.h"

namespace menu_item::midi {
class Preset : public Integer {
public:
	using Integer::Integer;

	int getMaxValue() const { return 128; } // Probably not needed cos we override below...

#if HAVE_OLED
	void drawInteger(int textWidth, int textHeight, int yPixel) {
		char buffer[12];
		char const* text;
		if (soundEditor.currentValue == 128) {
			text = "NONE";
		}
		else {
			intToString(soundEditor.currentValue + 1, buffer, 1);
			text = buffer;
		}
		OLED::drawStringCentred(text, yPixel + OLED_MAIN_TOPMOST_PIXEL, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
		                        textWidth, textHeight);
	}
#else
	void drawValue() {
		if (soundEditor.currentValue == 128) {
			numericDriver.setText("NONE");
		}
		else {
			numericDriver.setTextAsNumber(soundEditor.currentValue + 1);
		}
	}
#endif
	bool isRelevant(Sound* sound, int whichThing) {
		return currentSong->currentClip->output->type == INSTRUMENT_TYPE_MIDI_OUT;
	}

	void selectEncoderAction(int offset) {
		soundEditor.currentValue += offset;
		if (soundEditor.currentValue >= 129) {
			soundEditor.currentValue -= 129;
		}
		else if (soundEditor.currentValue < 0) {
			soundEditor.currentValue += 129;
		}
		Number::selectEncoderAction(offset);
	}
};
} // namespace menu_item::midi
