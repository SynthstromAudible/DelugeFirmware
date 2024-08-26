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
#include "gui/menu_item/integer.h"
#include "gui/menu_item/note/selected_note.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include <cstdint>

namespace deluge::gui::menu_item::note {
class IteranceDivisor final : public SelectedNote {
public:
	using SelectedNote::SelectedNote;

	[[nodiscard]] int32_t getMaxValue() const override { return 8; }
	[[nodiscard]] int32_t getMinValue() const override { return 1; }

	/// @brief Begin an editing session with this menu item.
	///
	/// Should make sure the menu's internal state matches the system and redraw the display.
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) final override { readValueAgain(); }

	void readCurrentValue() override {
		Note* leftMostNote = instrumentClipView.getLeftMostNotePressed();

		if (leftMostNote) {
			Iterance iterance = leftMostNote->getIterance();
			if (iterance == kDefaultIteranceValue) {
				// if we end up here in this menu, convert OFF to the default CUSTOM value 1of1
				// so we can make edits from here
				iterance = kCustomIteranceValue;
			}
			int32_t divisor = iterance.divisor;
			this->setValue(std::clamp<int32_t>(divisor, 1, 8));
		}
	}
	void writeCurrentValue() override {
		int32_t val = this->getValue();
		Note* leftMostNote = instrumentClipView.getLeftMostNotePressed();
		if (leftMostNote) {
			Iterance iterance = leftMostNote->getIterance();
			if (iterance == kDefaultIteranceValue) {
				// if we end up here in this menu, convert OFF to the default CUSTOM value 1of1
				// so we can make edits from here
				iterance = kCustomIteranceValue;
			}
			int32_t mask = (1 << val) - 1; // Creates a mask where the first 'divisor' bits are 1
			// Wipe the bits whose index is greater than the current divisor value
			int32_t newIteranceSteps = ((iterance.toInt() & 0xFF) & mask);
			instrumentClipView.adjustNoteIteranceWithFinalValue(Iterance{(uint8_t)val, newIteranceSteps});
		}
	}
};
} // namespace deluge::gui::menu_item::note
