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
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/drum/kit.h"
#include "gui/menu_item/selection.h"
#include "model/song/song.h"
#include "gui/ui/sound_editor.h"

namespace deluge::gui::menu_item::sequence {
class Direction final : public Selection {
public:
	using Selection::Selection;

	ModelStackWithNoteRow* getIndividualNoteRow(ModelStackWithTimelineCounter* modelStack) {
		auto* clip = dynamic_cast<InstrumentClip*>(modelStack->getTimelineCounter());
		if (!clip->affectEntire && clip->output->type == INSTRUMENT_TYPE_KIT) {
			Kit* kit = dynamic_cast<Kit*>(currentSong->currentClip->output);
			if (kit->selectedDrum != nullptr) {
				return clip->getNoteRowForDrum(modelStack, kit->selectedDrum); // Still might be NULL;
			}
		}
		return modelStack->addNoteRow(0, nullptr);
	}

	void readCurrentValue() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);

		if (modelStackWithNoteRow->getNoteRowAllowNull() != nullptr) {
			this->value_ = modelStackWithNoteRow->getNoteRow()->sequenceDirectionMode;
		}
		else {
			this->value_ = (dynamic_cast<InstrumentClip*>(currentSong->currentClip))->sequenceDirectionMode;
		}
	}

	void writeCurrentValue() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);
		if (modelStackWithNoteRow->getNoteRowAllowNull() != nullptr) {
			modelStackWithNoteRow->getNoteRow()->setSequenceDirectionMode(modelStackWithNoteRow, this->value_);
		}
		else {
			(dynamic_cast<InstrumentClip*>(currentSong->currentClip))
			    ->setSequenceDirectionMode(modelStackWithNoteRow->toWithTimelineCounter(), this->value_);
		}
	}

	size_t getNumOptions() {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);
		return modelStackWithNoteRow->getNoteRowAllowNull() != nullptr ? 4 : 3;
	}

	Sized<char const**> getOptions() override {
		static char const* sequenceDirectionOptions[] = {"FORWARD", "REVERSED", "PING-PONG", nullptr};

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);
		sequenceDirectionOptions[3] = modelStackWithNoteRow->getNoteRowAllowNull() != nullptr ? "NONE" : nullptr;
		return {sequenceDirectionOptions, getNumOptions()};
	}

	int checkPermissionToBeginSession(Sound* sound, int whichThing, ::MultiRange** currentRange) override {
		if (!(dynamic_cast<InstrumentClip*>(currentSong->currentClip))->affectEntire
		    && currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT
		    && ((dynamic_cast<Kit*>(currentSong->currentClip->output))->selectedDrum == nullptr)) {
			return MENU_PERMISSION_NO;
		}
		return MENU_PERMISSION_YES;
	}
};
} // namespace deluge::gui::menu_item::sequence
