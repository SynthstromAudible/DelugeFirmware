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
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "util/misc.h"

namespace deluge::gui::menu_item::sequence {
class Direction final : public Selection<kNumSequenceDirections + 1> {
public:
	using Selection::Selection;

	ModelStackWithNoteRow* getIndividualNoteRow(ModelStackWithTimelineCounter* modelStack) {
		auto* clip = static_cast<InstrumentClip*>(modelStack->getTimelineCounter());
		if (!clip->affectEntire && clip->output->type == InstrumentType::KIT) {
			Kit* kit = static_cast<Kit*>(currentSong->currentClip->output);
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
			this->setValue(modelStackWithNoteRow->getNoteRow()->sequenceDirectionMode);
		}
		else {
			this->setValue((static_cast<InstrumentClip*>(currentSong->currentClip))->sequenceDirectionMode);
		}
	}

	void writeCurrentValue() override {
		auto current_value = this->getValue<SequenceDirection>();
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);
		if (modelStackWithNoteRow->getNoteRowAllowNull() != nullptr) {
			modelStackWithNoteRow->getNoteRow()->setSequenceDirectionMode(modelStackWithNoteRow, current_value);
		}
		else {
			(static_cast<InstrumentClip*>(currentSong->currentClip))
			    ->setSequenceDirectionMode(modelStackWithNoteRow->toWithTimelineCounter(), current_value);
		}
	}

	static_vector<std::string_view, capacity()> getOptions() override {
		static_vector<std::string_view, capacity()> sequenceDirectionOptions = {"FORWARD", "REVERSED", "PING-PONG"};

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);
		if (modelStackWithNoteRow->getNoteRowAllowNull() != nullptr) {
			sequenceDirectionOptions.push_back("NONE");
		}

		return sequenceDirectionOptions;
	}

	MenuPermission checkPermissionToBeginSession(Sound* sound, int32_t whichThing,
	                                             ::MultiRange** currentRange) override {
		if (!(static_cast<InstrumentClip*>(currentSong->currentClip))->affectEntire
		    && currentSong->currentClip->output->type == InstrumentType::KIT
		    && ((static_cast<Kit*>(currentSong->currentClip->output))->selectedDrum == nullptr)) {
			return MenuPermission::NO;
		}
		return MenuPermission::YES;
	}
};
} // namespace deluge::gui::menu_item::sequence
