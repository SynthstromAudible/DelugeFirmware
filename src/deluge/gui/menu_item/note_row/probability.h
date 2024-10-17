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
#include "gui/menu_item/note_row/selected_note_row.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::note_row {
class Probability final : public SelectedNoteRow {
public:
	using SelectedNoteRow::SelectedNoteRow;

	[[nodiscard]] int32_t getMaxValue() const override { return (kNumProbabilityValues | 127); }
	[[nodiscard]] int32_t getMinValue() const override { return 1; }

	/// @brief Begin an editing session with this menu item.
	///
	/// Should make sure the menu's internal state matches the system and redraw the display.
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) final override { readValueAgain(); }

	void readCurrentValue() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);

		if (modelStackWithNoteRow->getNoteRowAllowNull() != nullptr) {
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
			this->setValue(noteRow->probabilityValue);
			updateDisplay();
		}
	}

	void selectEncoderAction(int32_t offset) final override {
		int32_t newValue = instrumentClipView.setNoteRowProbabilityWithOffset(offset);
		if (newValue != -1) {
			this->setValue(newValue);
			updateDisplay();
		}
	}

	void drawPixelsForOled() {
		char buffer[20];

		int32_t probability = this->getValue();
		bool latching = false;

		// if it's a latching probability, remove latching from value
		if (probability > kNumProbabilityValues) {
			probability &= 127;
			latching = true;
		}

		sprintf(buffer, "%d%%", probability * 5);

		if (latching) {
			strcat(buffer, " (L)");
		}

		deluge::hid::display::OLED::main.drawStringCentred(buffer, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
		                                                   kTextHugeSizeY);
	}

	void drawValue() final override {
		char buffer[20];

		int32_t probability = this->getValue();
		bool latching = false;

		// if it's a latching probability, remove latching from value
		if (probability > kNumProbabilityValues) {
			probability &= 127;
			latching = true;
		}

		intToString(probability * 5, buffer);

		display->setText(buffer, true, latching ? 3 : 255);
	}

	void writeCurrentValue() override { ; }
};
} // namespace deluge::gui::menu_item::note_row
