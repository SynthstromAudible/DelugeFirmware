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

namespace deluge::gui::menu_item::note_row {
class IteranceStepToggle : public Toggle {
public:
	using Toggle::Toggle;

	IteranceStepToggle(l10n::String newName, l10n::String title, uint8_t newIndex) : Toggle(newName, title) {
		this->index = newIndex;
	}

	ModelStackWithNoteRow* getIndividualNoteRow(ModelStackWithTimelineCounter* modelStack) {
		auto* clip = static_cast<InstrumentClip*>(modelStack->getTimelineCounter());
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
		                             modelStack); // don't create

		bool isKit = clip->output->type == OutputType::KIT;

		if (!isKit) {
			if (!modelStackWithNoteRow->getNoteRowAllowNull()) { // if note row doesn't exist yet, create it
				modelStackWithNoteRow =
				    instrumentClipView.createNoteRowForYDisplay(modelStack, instrumentClipView.lastAuditionedYDisplay);
			}
		}

		return modelStackWithNoteRow;
	}

	void updateDisplay() {
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			drawValue();
		}
	}

	void readCurrentValue() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);

		if (modelStackWithNoteRow->getNoteRowAllowNull() != nullptr) {
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
			int32_t iterance = noteRow->iteranceValue;
			if (iterance == kDefaultIteranceValue) {
				// if entered custom menu, set to custom value if none set
				iterance = kCustomIteranceValue;
			}
			this->setValue((iterance & (1 << index)) != 0);
			updateDisplay();
		}
	}
	void writeCurrentValue() override {
		bool value = this->getValue();
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);

		if (modelStackWithNoteRow->getNoteRowAllowNull() != nullptr) {
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
			int32_t iterance = noteRow->iteranceValue;
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
			noteRow->iteranceValue = iterance;
			this->setValue((iterance & (1 << index)) != 0);
			updateDisplay();
		}
	}

	uint8_t index;

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);

		if (modelStackWithNoteRow->getNoteRowAllowNull() != nullptr) {
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
			int32_t iterance = noteRow->iteranceValue;
			return (iterance == 0 && index == 0) || (iterance >> 8) > index;
		}
		return false;
	}
};
} // namespace deluge::gui::menu_item::note_row
