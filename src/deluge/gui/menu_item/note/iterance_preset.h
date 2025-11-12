/*
 * Copyright (c) 2024 Sean Ditny / Raúl Muñoz
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
#include "gui/menu_item/note/selected_note.h"
#include "gui/menu_item/submenu.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/oled.h"
#include "model/note/note.h"

extern gui::menu_item::Submenu noteCustomIteranceRootMenu;

namespace deluge::gui::menu_item::note {

using namespace deluge::hid::display;

class IterancePreset final : public SelectedNote {
public:
	using SelectedNote::SelectedNote;

	[[nodiscard]] int32_t getMaxValue() const override { return kNumIterancePresets + 1; }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }

	/// @brief Begin an editing session with this menu item.
	///
	/// Should make sure the menu's internal state matches the system and redraw the display.
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) final override { readValueAgain(); }

	void readCurrentValue() override {
		Note* leftMostNote = instrumentClipView.getLeftMostNotePressed();

		if (leftMostNote) {
			// Convert value to preset to choose from, if preset not found, then maybe it is CUSTOM
			int32_t preset = leftMostNote->getIterance().toPresetIndex();
			this->setValue(preset);
		}
	}

	void selectEncoderAction(int32_t offset) override {
		instrumentClipView.adjustNoteIteranceWithOffset(offset);
		readValueAgain();
	}

	MenuItem* selectButtonPress() override {
		int32_t iterancePreset = this->getValue();
		if (iterancePreset == kCustomIterancePreset) {
			// If the "CUSTOM" item is in focus, clicking the Select encoder will
			// enter the editor for the custom iterance
			return &noteCustomIteranceRootMenu;
		}
		return nullptr;
	}

	void drawPixelsForOled() override {
		const std::string value = getIteranceDisplayValue("%d of %d");
		OLED::main.drawStringCentred(value.data(), 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX, kTextHugeSizeY);
	}

	void renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) override {
		const std::string value = getIteranceDisplayValue("%d:%d");
		OLED::main.drawStringCentered(value.data(), slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset,
		                              kTextSpacingX, kTextSpacingY, slot.width);
	}

	void drawValue() override {
		const std::string value = getIteranceDisplayValue("%dof%d");
		display->setText(value);
	}

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		SelectedNote::configureRenderingOptions(options);
		options.notification_value = getIteranceDisplayValue("%d of %d");
	}

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

} // namespace deluge::gui::menu_item::note
