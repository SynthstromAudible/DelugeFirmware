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
#include "gui/views/instrument_clip_view.h"
#include "hid/display/oled.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::sequence {
class Direction final : public Selection {
public:
	using Selection::Selection;

	bool shouldEnterSubmenu() {
		if (getCurrentUI() == &soundEditor && soundEditor.inNoteRowEditor() && !isUIModeActive(UI_MODE_AUDITIONING)) {
			display->displayPopup("Select Row");
			return false;
		}
		return true;
	}

	ModelStackWithNoteRow* getIndividualNoteRow(ModelStackWithTimelineCounter* modelStack) {
		auto* clip = static_cast<InstrumentClip*>(modelStack->getTimelineCounter());
		if (!clip->affectEntire && clip->output->type == OutputType::KIT) {
			Kit* kit = getCurrentKit();
			if (kit->selectedDrum != nullptr) {
				return clip->getNoteRowForDrum(modelStack, kit->selectedDrum); // Still might be NULL;
			}
		}
		else if (clip->output->type != OutputType::KIT) {
			if (soundEditor.selectedNoteRow) {
				// get model stack with note row but don't create note row if it doesn't exist
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay, modelStack);
				if (!modelStackWithNoteRow->getNoteRowAllowNull()) { // if note row doesn't exist yet, create it
					modelStackWithNoteRow = instrumentClipView.createNoteRowForYDisplay(
					    modelStack, instrumentClipView.lastAuditionedYDisplay);
				}
				return modelStackWithNoteRow;
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

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
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

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override {
		OutputType outputType = getCurrentOutputType();
		if (!getCurrentInstrumentClip()->affectEntire && outputType == OutputType::KIT
		    && (getCurrentKit()->selectedDrum == nullptr)) {
			return MenuPermission::NO;
		}
		else if (outputType != OutputType::KIT) {
			soundEditor.selectedNoteRow = isUIModeActive(UI_MODE_AUDITIONING);
		}
		return MenuPermission::YES;
	}

	void renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) override {
		using namespace deluge::hid::display;
		oled_canvas::Canvas& image = OLED::main;

		const auto current_value = this->getValue<SequenceDirection>();
		if (current_value == SequenceDirection::OBEY_PARENT) {
			return Selection::renderInHorizontalMenu(slot);
		}

		const uint8_t icon_y = slot.start_y + kHorizontalMenuSlotYOffset;
		if (current_value == SequenceDirection::PINGPONG) {
			image.drawIconCentered(OLED::directionIcon, slot.start_x + 2, slot.width, icon_y);
			image.drawIconCentered(OLED::directionIcon, slot.start_x - 2, slot.width, icon_y, true);
		}
		else {
			image.drawIconCentered(OLED::directionIcon, slot.start_x, slot.width, icon_y,
			                       current_value == SequenceDirection::REVERSE);
		}
	}
};
} // namespace deluge::gui::menu_item::sequence
