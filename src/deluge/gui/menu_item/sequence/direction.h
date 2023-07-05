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

namespace menu_item::sequence {
class Direction final : public Selection {
public:
	using Selection::Selection;

	ModelStackWithNoteRow* getIndividualNoteRow(ModelStackWithTimelineCounter* modelStack) {
		InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();
		if (!clip->affectEntire && clip->output->type == INSTRUMENT_TYPE_KIT) {
			Kit* kit = (Kit*)currentSong->currentClip->output;
			if (kit->selectedDrum) {
				return clip->getNoteRowForDrum(modelStack, kit->selectedDrum); // Still might be NULL;
			}
		}
		return modelStack->addNoteRow(0, NULL);
	}

	void readCurrentValue() {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);

		if (modelStackWithNoteRow->getNoteRowAllowNull()) {
			soundEditor.currentValue = modelStackWithNoteRow->getNoteRow()->sequenceDirectionMode;
		}
		else {
			soundEditor.currentValue = ((InstrumentClip*)currentSong->currentClip)->sequenceDirectionMode;
		}
	}

	void writeCurrentValue() {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);
		if (modelStackWithNoteRow->getNoteRowAllowNull()) {
			modelStackWithNoteRow->getNoteRow()->setSequenceDirectionMode(modelStackWithNoteRow,
			                                                              soundEditor.currentValue);
		}
		else {
			((InstrumentClip*)currentSong->currentClip)
			    ->setSequenceDirectionMode(modelStackWithNoteRow->toWithTimelineCounter(), soundEditor.currentValue);
		}
	}

	int getNumOptions() {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);
		return modelStackWithNoteRow->getNoteRowAllowNull() ? 4 : 3;
	}

	char const** getOptions() {
		static char const* sequenceDirectionOptions[] = {"FORWARD", "REVERSED", "PING-PONG", NULL, NULL};

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);
		sequenceDirectionOptions[3] = modelStackWithNoteRow->getNoteRowAllowNull() ? "NONE" : NULL;
		return sequenceDirectionOptions;
	}

	int checkPermissionToBeginSession(Sound* sound, int whichThing, MultiRange** currentRange) {
		if (!((InstrumentClip*)currentSong->currentClip)->affectEntire
		    && currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT
		    && !((Kit*)currentSong->currentClip->output)->selectedDrum) {
			return MENU_PERMISSION_NO;
		}
		else {
			return MENU_PERMISSION_YES;
		}
	}
};
} // namespace menu_item::sequence
