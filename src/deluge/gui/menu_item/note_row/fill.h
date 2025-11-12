/*
 * Copyright (c) 2024 Sean Ditny
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
#include "gui/menu_item/note_row/selected_note_row.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/oled.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::note_row {

using namespace deluge::hid::display;

class Fill final : public SelectedNoteRow {
public:
	using SelectedNoteRow::SelectedNoteRow;

	[[nodiscard]] int32_t getMaxValue() const override { return FillMode::FILL; }
	[[nodiscard]] int32_t getMinValue() const override { return FillMode::OFF; }

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
			this->setValue(noteRow->fillValue);
			updateDisplay();
		}
	}

	void selectEncoderAction(int32_t offset) final override {
		int32_t newValue = instrumentClipView.setNoteRowFillWithOffset(offset);
		if (newValue != -1) {
			this->setValue(newValue);
			updateDisplay();
			if (currentSong->isFillModeActive()) {
				uiNeedsRendering(&instrumentClipView);
			}
		}
	}

	void drawPixelsForOled() override {
		OLED::main.drawStringCentred(instrumentClipView.getFillString(this->getValue()), 18 + OLED_MAIN_TOPMOST_PIXEL,
		                             kTextHugeSpacingX, kTextHugeSizeY);
	}

	void renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) override {
		oled_canvas::Canvas& image = OLED::main;

		const uint8_t value = getValue();
		const std::string str = value == OFF ? "OFF" : "FILL";
		image.drawStringCentered(str.data(), slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset, kTextSpacingX,
		                         kTextSpacingY, slot.width);

		if (value == NOT_FILL) {
			const uint8_t center_y = slot.start_y + kHorizontalMenuSlotYOffset + 4;
			const uint8_t line_start_x = slot.start_x + 2;
			const uint8_t line_end_x = slot.start_x + slot.width - 4;
			for (uint8_t x = line_start_x; x <= line_end_x; x++) {
				image.clearPixel(x, center_y - 1);
				image.clearPixel(x, center_y + 1);
			}
			image.drawHorizontalLine(center_y, line_start_x, line_end_x);
		}
	}

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		SelectedNoteRow::configureRenderingOptions(options);
		options.notification_value = instrumentClipView.getFillString(getValue());
	}

	void drawValue() override { display->setText(instrumentClipView.getFillString(this->getValue())); }

	void writeCurrentValue() override { ; }
};
} // namespace deluge::gui::menu_item::note_row
