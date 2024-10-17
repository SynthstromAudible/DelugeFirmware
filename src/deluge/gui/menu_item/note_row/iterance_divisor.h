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
#include "gui/menu_item/note_row/selected_note_row.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include <cstdint>

namespace deluge::gui::menu_item::note_row {
class IteranceDivisor final : public SelectedNoteRow {
public:
	using SelectedNoteRow::SelectedNoteRow;

	[[nodiscard]] int32_t getMaxValue() const override { return 8; }
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
			Iterance iterance = noteRow->iteranceValue;
			if (iterance == kDefaultIteranceValue) {
				// if we end up here in this menu, convert OFF to the default CUSTOM value 1of1
				// so we can make edits from here
				iterance = kCustomIteranceValue;
			}
			int32_t divisor = iterance.divisor;
			this->setValue(std::clamp<int32_t>(divisor, 1, 8));
			updateDisplay();
		}
	}
	void writeCurrentValue() override {
		int32_t val = this->getValue();
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);

		if (modelStackWithNoteRow->getNoteRowAllowNull() != nullptr) {
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
			Iterance iterance = noteRow->iteranceValue;
			if (iterance == kDefaultIteranceValue) {
				// if we end up here in this menu, convert OFF to the default CUSTOM value 1of1
				// so we can make edits from here
				iterance = kCustomIteranceValue;
			}
			int32_t mask = (1 << val) - 1; // Creates a mask where the first 'divisor' bits are 1
			// Wipe the bits whose index is greater than the current divisor value
			int32_t newIteranceSteps = ((iterance.toInt() & 0xFF) & mask);
			instrumentClipView.setNoteRowIteranceWithFinalValue(Iterance{(uint8_t)val, newIteranceSteps});
		}
	}
};
} // namespace deluge::gui::menu_item::note_row
