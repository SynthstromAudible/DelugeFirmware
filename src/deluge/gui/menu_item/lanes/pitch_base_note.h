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
#include "hid/display/oled.h"
#include "model/clip/instrument_clip.h"
#include "model/sequencing/lanes_engine.h"
#include "model/song/song.h"
#include "util/functions.h"

namespace deluge::gui::menu_item::lanes {

class PitchBaseNote final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMaxValue() const override { return 127; }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return NUMBER; }

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		char buffer[8];
		noteCodeToString(this->getValue(), buffer, nullptr, true);
		deluge::hid::display::OLED::main.drawStringCentered(
		    buffer, slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset, kTextSpacingX, kTextSpacingY, slot.width);
	}

	void readCurrentValue() override {
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (clip->lanesEngine) {
			this->setValue(clip->lanesEngine->baseNote);
		}
	}

	void writeCurrentValue() override {
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (clip->lanesEngine) {
			clip->lanesEngine->baseNote = static_cast<int16_t>(this->getValue());
		}
	}

	void selectEncoderAction(int32_t offset) override {
		int32_t current = this->getValue();
		int32_t next = current + offset;

		// In scale mode, skip non-scale notes
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (clip->isScaleModeClip()) {
			while (next >= 0 && next <= 127 && !currentSong->isYNoteAllowed(next, true)) {
				next += offset;
			}
		}

		if (next < 0 || next > 127) {
			return; // At boundary, don't wrap
		}
		this->setValue(next);
		writeCurrentValue();

		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			drawValue();
		}
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override {
		return MenuPermission::YES;
	}

protected:
	void drawPixelsForOled() override {
		char buffer[8];
		noteCodeToString(this->getValue(), buffer, nullptr, true);
		deluge::hid::display::OLED::main.drawStringCentred(buffer, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
		                                                   kTextHugeSizeY);
	}

	void drawValue() override {
		char buffer[8];
		noteCodeToString(this->getValue(), buffer, nullptr, true);
		display->setText(buffer);
	}
};

} // namespace deluge::gui::menu_item::lanes
