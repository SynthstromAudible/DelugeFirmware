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
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::sequence {
class Direction final : public Selection {
public:
	using Selection::Selection;

	ModelStackWithNoteRow* getIndividualNoteRow(ModelStackWithTimelineCounter* modelStack) {
		auto* clip = static_cast<InstrumentClip*>(modelStack->getTimelineCounter());
		if (!clip->affectEntire && clip->output->type == OutputType::KIT) {
			Kit* kit = getCurrentKit();
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
			this->setValue(getCurrentInstrumentClip()->sequenceDirectionMode);
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
			getCurrentInstrumentClip()->setSequenceDirectionMode(modelStackWithNoteRow->toWithTimelineCounter(),
			                                                     current_value);
		}
	}

	deluge::vector<std::string_view> getOptions() override {
		deluge::vector<std::string_view> sequenceDirectionOptions = {
		    l10n::getView(l10n::String::STRING_FOR_FORWARD),
		    l10n::getView(l10n::String::STRING_FOR_REVERSED),
		    l10n::getView(l10n::String::STRING_FOR_PING_PONG),
		};

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);
		if (modelStackWithNoteRow->getNoteRowAllowNull() != nullptr) {
			sequenceDirectionOptions.push_back(l10n::getView(l10n::String::STRING_FOR_NONE));
		}

		return sequenceDirectionOptions;
	}

	MenuPermission checkPermissionToBeginSession(Sound* sound, int32_t whichThing,
	                                             ::MultiRange** currentRange) override {
		if (!getCurrentInstrumentClip()->affectEntire && getCurrentOutputType() == OutputType::KIT
		    && (getCurrentKit()->selectedDrum == nullptr)) {
			return MenuPermission::NO;
		}
		return MenuPermission::YES;
	}
};
} // namespace deluge::gui::menu_item::sequence
