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
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "util/functions.h"

namespace deluge::gui::menu_item::note {
class Iterance final : public SelectedNote {
public:
	using SelectedNote::SelectedNote;

	[[nodiscard]] int32_t getMaxValue() const override { return kNumIterationPresets + 1; }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }

	/// @brief Begin an editing session with this menu item.
	///
	/// Should make sure the menu's internal state matches the system and redraw the display.
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) final override { readValueAgain(); }

	void readCurrentValue() override {
		Note* leftMostNote = instrumentClipView.getLeftMostNotePressed();

		if (leftMostNote) {
			int32_t preset = getIterancePresetFromValue(leftMostNote->getIterance());
			this->setValue(preset);
		}
	}

	void selectEncoderAction(int32_t offset) final override {
		instrumentClipView.adjustNoteIterance(offset);
		readValueAgain();
	}

	// MenuItem* selectButtonPress() override {
	// 	int32_t iterancePreset = this->getValue();
	// 	if (iterancePreset == kCustomIterancePreset) {
	// 		return &noteCustomIteranceRootMenu;
	// 	}
	// 	return nullptr;
	// }

	void drawPixelsForOled() {
		char buffer[20];

		int32_t iterancePreset = this->getValue();

		if (iterancePreset == kDefaultIteranceValue) {
			strcpy(buffer, "OFF");
		}
		else if (iterancePreset == kCustomIterancePreset) {
			strcpy(buffer, "CUSTOM");
		}
		else {
			int32_t divisor, iterationBitsWithinDivisor;
			dissectIterationDependence(iterancePresets[iterancePreset - 1], &divisor, &iterationBitsWithinDivisor);
			int32_t i = divisor;
			for (; i >= 0; i--) {
				if (iterationBitsWithinDivisor & (1 << i)) {
					break;
				}
			}
			sprintf(buffer, "%d of %d", i + 1, divisor);
		}

		deluge::hid::display::OLED::main.drawStringCentred(buffer, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
		                                                   kTextHugeSizeY);
	}

	void drawValue() final override {
		char buffer[20];

		int32_t iterancePreset = this->getValue();

		if (iterancePreset == kDefaultIteranceValue) {
			strcpy(buffer, "OFF");
		}
		else if (iterancePreset == kCustomIterancePreset) {
			strcpy(buffer, "CUSTOM");
		}
		else {
			int32_t divisor, iterationBitsWithinDivisor;
			dissectIterationDependence(iterancePresets[iterancePreset - 1], &divisor, &iterationBitsWithinDivisor);
			int32_t i = divisor;
			for (; i >= 0; i--) {
				if (iterationBitsWithinDivisor & (1 << i)) {
					break;
				}
			}
			sprintf(buffer, "%dof%d", i + 1, divisor);
		}

		display->setText(buffer);
	}

	void writeCurrentValue() override { ; }
};

} // namespace deluge::gui::menu_item::note
