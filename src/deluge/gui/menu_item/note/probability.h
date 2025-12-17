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
#include "definitions_cxx.hpp"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/note/selected_note.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::note {
class Probability final : public SelectedNote {
public:
	using SelectedNote::SelectedNote;

	[[nodiscard]] int32_t getMaxValue() const override { return (kNumProbabilityValues | 127); }
	[[nodiscard]] int32_t getMinValue() const override { return 1; }

	/// @brief Begin an editing session with this menu item.
	///
	/// Should make sure the menu's internal state matches the system and redraw the display.
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) final override { readValueAgain(); }

	void readCurrentValue() override {
		Note* leftMostNote = instrumentClipView.getLeftMostNotePressed();

		if (leftMostNote) {
			this->setValue(leftMostNote->getProbability());
		}
	}

	void selectEncoderAction(int32_t offset) override {
		instrumentClipView.adjustNoteProbabilityWithOffset(offset);
		readValueAgain();
	}

	void drawPixelsForOled() override {
		char buffer[20];
		bool latching = false;

		intToString(getProbabilityValue(latching), buffer);
		strcat(buffer, "%");
		if (latching) {
			strcat(buffer, " (L)");
		}

		OLED::main.drawStringCentred(buffer, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX, kTextHugeSizeY);
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		char buffer[20];
		bool latching = false;

		intToString(getProbabilityValue(latching), buffer);
		strcat(buffer, latching ? "L" : "%");

		OLED::main.drawStringCentered(buffer, slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset, kTextSpacingX,
		                              kTextSpacingY, slot.width);
	}

	void drawValue() override {
		char buffer[20];
		bool latching = false;

		intToString(getProbabilityValue(latching), buffer);

		display->setText(buffer, true, latching ? 3 : 255);
	}

	void getNotificationValue(StringBuf& valueBuf) override {
		bool latching = false;
		valueBuf.appendInt(getProbabilityValue(latching));
		valueBuf.append("%");

		if (latching) {
			valueBuf.append(" ltch");
		}
	}

	void writeCurrentValue() override { ; }

private:
	int32_t getProbabilityValue(bool& latching) {
		int32_t probability = this->getValue();

		// if it's a latching probability, remove latching from value
		if (probability > kNumProbabilityValues) {
			probability &= 127;
			latching = true;
		}

		return probability * 5;
	}
};
} // namespace deluge::gui::menu_item::note
