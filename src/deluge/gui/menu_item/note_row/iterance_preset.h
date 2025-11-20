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
#include "gui/menu_item/menu_item.h"
#include "gui/menu_item/note_row/selected_note_row.h"
#include "gui/menu_item/submenu.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/oled.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "util/lookuptables/lookuptables.h"

extern gui::menu_item::Submenu noteRowCustomIteranceRootMenu;

namespace deluge::gui::menu_item::note_row {

using namespace deluge::hid::display;

class IterancePreset final : public SelectedNoteRow {
public:
	using SelectedNoteRow::SelectedNoteRow;

	[[nodiscard]] int32_t getMaxValue() const override { return kNumIterancePresets + 1; }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }

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
			// Convert value to preset to choose from, if preset not found, then maybe it is CUSTOM
			int32_t preset = noteRow->iteranceValue.toPresetIndex();
			this->setValue(preset);
			updateDisplay();
		}
	}

	void selectEncoderAction(int32_t offset) final override {
		int32_t newValue = instrumentClipView.setNoteRowIteranceWithOffset(offset);
		if (newValue != -1) {
			// Convert value to preset to choose from, if preset not found, then maybe it is CUSTOM
			int32_t preset = Iterance::fromInt(newValue).toPresetIndex();
			this->setValue(preset);
			updateDisplay();
		}
	}

	MenuItem* selectButtonPress() override {
		int32_t iterancePreset = this->getValue();
		if (iterancePreset == kCustomIterancePreset) {
			// If the "CUSTOM" item is in focus, clicking the Select encoder will
			// enter the editor for the custom iterance
			return &noteRowCustomIteranceRootMenu;
		}
		return nullptr;
	}

	void drawPixelsForOled() override {
		const std::string value = getIteranceDisplayValue("%d of %d");
		OLED::main.drawStringCentred(value.data(), 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX, kTextHugeSizeY);
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		const std::string value = getIteranceDisplayValue("%d:%d");
		OLED::main.drawStringCentered(value.data(), slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset,
		                              kTextSpacingX, kTextSpacingY, slot.width);
	}

	void drawValue() override {
		const std::string value = getIteranceDisplayValue("%dof%d");
		display->setText(value);
	}

	void getNotificationValue(StringBuf& valueBuf) override { valueBuf.append(getIteranceDisplayValue("%d of %d")); }

	void writeCurrentValue() override { ; }

private:
	std::string getIteranceDisplayValue(const std::string& format) {
		char buffer[20];

		int32_t iterancePreset = this->getValue();

		if (iterancePreset == kDefaultIterancePreset) {
			strcpy(buffer, "OFF");
		}
		else if (iterancePreset == kCustomIterancePreset) {
			strcpy(buffer, "CUSTOM");
		}
		else {
			Iterance iterance = iterancePresets[iterancePreset - 1];
			int32_t i = iterance.divisor;
			for (; i >= 0; i--) {
				// try to find which iteration step index is active
				if (iterance.iteranceStep[i]) {
					break;
				}
			}
			sprintf(buffer, format.data(), i + 1, iterance.divisor);
		}

		return std::string(buffer);
	}
};

} // namespace deluge::gui::menu_item::note_row
