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
#include "gui/menu_item/toggle.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note.h"
#include "model/note/note_row.h"

namespace deluge::gui::menu_item::note {
class IteranceStepToggle : public Toggle {
public:
	using Toggle::Toggle;

	IteranceStepToggle(l10n::String newName, l10n::String title, uint8_t newIndex) : Toggle(newName, title) {
		this->index = newIndex;
	}

	void readCurrentValue() override {
		Note* leftMostNote = instrumentClipView.getLeftMostNotePressed();

		if (leftMostNote) {
			int32_t iterance = leftMostNote->getIterance();
			if (iterance == kDefaultIteranceValue) {
				// if entered custom menu, set to custom value if none set
				iterance = kCustomIteranceValue;
			}
			this->setValue((iterance & (1 << index)) != 0);
		}
	}
	void writeCurrentValue() override {
		bool value = this->getValue();
		Note* leftMostNote = instrumentClipView.getLeftMostNotePressed();

		if (leftMostNote) {
			int32_t iterance = leftMostNote->getIterance();
			if (iterance == kDefaultIteranceValue) {
				// if entered custom menu, set to custom value if none set
				iterance = kCustomIteranceValue;
			}
			if (value) {
				iterance |= (1 << index);
			}
			else {
				iterance &= ~(1 << index);
			}
			leftMostNote->setIterance(iterance);
		}
	}

	uint8_t index;

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		Note* leftMostNote = instrumentClipView.getLeftMostNotePressed();

		if (leftMostNote) {
			int32_t iterance = leftMostNote->getIterance();
			return (iterance == 0 && index == 0) || (iterance >> 8) > index;
		}
		return false;
	}
};
} // namespace deluge::gui::menu_item::note
