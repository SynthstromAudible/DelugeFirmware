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
#include "gui/menu_item/toggle.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note.h"

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
			Iterance iterance = leftMostNote->getIterance();
			if (iterance == kDefaultIteranceValue) {
				// if we end up here in this menu, convert OFF to the default CUSTOM value 1of1
				// so we can make edits from here
				iterance = kCustomIteranceValue;
			}
			this->setValue(iterance.iteranceStep[index]);
		}
	}
	void writeCurrentValue() override {
		bool value = this->getValue();
		Note* leftMostNote = instrumentClipView.getLeftMostNotePressed();

		if (leftMostNote) {
			Iterance iterance = leftMostNote->getIterance();
			if (iterance == kDefaultIteranceValue) {
				// if we end up here in this menu, convert OFF to the default CUSTOM value 1of1
				// so we can make edits from here
				iterance = kCustomIteranceValue;
			}
			int32_t newIteranceSteps = iterance.toInt() & 0xFF;
			if (value) {
				newIteranceSteps |= (1 << index);
			}
			else {
				newIteranceSteps &= ~(1 << index);
			}
			instrumentClipView.adjustNoteIteranceWithFinalValue(Iterance{iterance.divisor, newIteranceSteps});
		}
	}

	uint8_t index;

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		Note* leftMostNote = instrumentClipView.getLeftMostNotePressed();

		if (leftMostNote) {
			Iterance iterance = leftMostNote->getIterance();
			// Only show this iteration step if its index is smaller than current divisor value
			return (iterance == kDefaultIteranceValue && index == 0) || iterance.divisor > index;
		}
		return false;
	}
};
} // namespace deluge::gui::menu_item::note
