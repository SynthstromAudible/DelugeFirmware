/*
 * Copyright © 2026 Sean Ditny
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
#include "hid/buttons.h"
#include "hid/display/oled.h"
#include "model/clip/instrument_clip.h"
#include "model/song/note_spelling.h"
#include "model/song/song.h"
#include "util/functions.h"

namespace deluge::gui::menu_item::note {

class Placement final : public SelectedNote {
public:
	enum class Kind { POSITION, LENGTH, NUDGE, REPEATS, PITCH };
	Placement(l10n::String name, l10n::String title, Kind kind) : SelectedNote(name, title), kind_(kind) {}

	int32_t getOccupiedSlots() const override { return kind_ == Kind::PITCH ? 2 : 1; }

	bool shouldEnterSubmenu() override {
		return (kind_ == Kind::PITCH
		        && (instrumentClipView.numEditPadPresses || instrumentClipView.hasFloatingNoteSelection()))
		       || SelectedNote::shouldEnterSubmenu();
	}

	void readCurrentValue() override {
		setValue(0);
		valid_ = false;
		auto targets = instrumentClipView.get_note_edit_targets();
		const EditPadPress* selected = nullptr;
		int32_t count = 0;
		for (const auto& target : targets) {
			bool floating = kind_ == Kind::PITCH && !instrumentClipView.numEditPadPresses
			                && instrumentClipView.remembered_note_floating[&target - targets.data()];
			if (target.isActive || floating) {
				++count;
				if (!selected || target.intendedPos < selected->intendedPos) {
					selected = &target;
				}
			}
		}
		if (!selected || (kind_ == Kind::REPEATS && count != 1)) {
			return;
		}
		valid_ = true;
		char memory[MODEL_STACK_MAX_SIZE];
		auto* stack = currentSong->setupModelStackWithCurrentClip(memory);
		auto* row = instrumentClipView.get_note_edit_row(stack, *selected);
		switch (kind_) {
		case Kind::POSITION:
			// One-based grid columns, counted from the start of the clip.
			setValue(selected->intendedPos / currentSong->xZoom[NAVIGATION_CLIP] + 1);
			break;
		case Kind::LENGTH: {
			auto* noteRow = row->getNoteRowAllowNull();
			auto* note = noteRow
			                 ? noteRow->notes.getElement(noteRow->notes.search(selected->intendedPos, GREATER_OR_EQUAL))
			                 : nullptr;
			valid_ = note != nullptr;
			if (note) {
				setValue(note->length);
			}
			break;
		}
		case Kind::NUDGE: {
			int32_t value = 0;
			instrumentClipView.nudgeNotes(0, false, &value);
			setValue(value);
			break;
		}
		case Kind::REPEATS:
			setValue(instrumentClipView.editNoteRepeat(0, false));
			break;
		case Kind::PITCH:
			setValue(row->noteRowId);
			break;
		}
	}

	void selectEncoderAction(int32_t offset) override {
		switch (kind_) {
		case Kind::POSITION:
			instrumentClipView.moveNotePosition(offset);
			break;
		case Kind::LENGTH:
			instrumentClipView.adjustNoteLength(offset);
			break;
		case Kind::NUDGE:
			instrumentClipView.nudgeNotes(offset, false);
			break;
		case Kind::REPEATS:
			instrumentClipView.editNoteRepeat(offset, false);
			break;
		case Kind::PITCH: {
			bool fine = Buttons::isAnyOfButtonsPressed({hid::button::SELECT_ENC, hid::button::SHIFT});
			if (fine) {
				// Fine edits stay one semitone per encoder event, including with Shift acceleration.
				offset = (offset > 0) - (offset < 0);
				Buttons::selectButtonPressUsedUp = true;
			}
			instrumentClipView.moveNotePitch(offset, fine);
			break;
		}
		}
		readValueAgain();
	}

protected:
	int32_t getMaxValue() const override { return INT32_MAX; }
	RenderingStyle getRenderingStyle() const override { return NUMBER; }
	void formatValue(char* buffer) {
		if (!valid_) {
			strcpy(buffer, "--");
		}
		else if (kind_ == Kind::LENGTH) {
			intToString(getValue(), buffer);
			strcat(buffer, "t");
		}
		else if (kind_ == Kind::PITCH && getCurrentInstrumentClip()->output->type != OutputType::KIT) {
			noteCodeToString(getValue(), buffer, nullptr, true, useSharpsForNoteNames());
		}
		else {
			intToString(getValue(), buffer);
		}
	}
	void renderInHorizontalMenu(const SlotPosition& slot) override {
		char buffer[kNoteCodeStringBufferSize];
		formatValue(buffer);
		hid::display::OLED::main.drawStringCentered(buffer, slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset,
		                                            kTextTitleSpacingX, kTextTitleSizeY, slot.width);
	}
	void drawPixelsForOled() override {
		char buffer[kNoteCodeStringBufferSize];
		formatValue(buffer);
		hid::display::OLED::main.drawStringCentered(buffer, 0, 24, kTextTitleSpacingX, kTextTitleSizeY,
		                                            OLED_MAIN_WIDTH_PIXELS);
	}
	void drawValue() override {
		char buffer[kNoteCodeStringBufferSize];
		formatValue(buffer);
		display->setScrollingText(buffer);
	}
	void getNotificationValue(StringBuf& value) override {
		char buffer[kNoteCodeStringBufferSize];
		formatValue(buffer);
		value.append(buffer);
	}

private:
	Kind kind_;
	bool valid_ = false;
};
} // namespace deluge::gui::menu_item::note
