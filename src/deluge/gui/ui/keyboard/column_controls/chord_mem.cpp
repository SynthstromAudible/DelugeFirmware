/*
 * Copyright © 2016-2024 Synthstrom Audible Limited
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

#include "chord_mem.h"
#include "gui/ui/keyboard/layout/column_controls.h"
#include "hid/buttons.h"
#include "model/song/song.h"

namespace deluge::gui::ui::keyboard::controls {

void ChordMemColumn::renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column) {
	uint8_t otherChannels = 0;
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		bool chord_selected = y == activeChordMem;
		uint8_t chord_slot_filled = chordMemNoteCount[y] > 0 ? 0x7f : 0;
		otherChannels = chord_selected ? 0xf0 : 0;
		uint8_t base = chord_selected ? 0xff : chord_slot_filled;
		image[y][column] = {otherChannels, base, base};
	}
}

bool ChordMemColumn::handleVerticalEncoder(int8_t pad, int32_t offset) {
	return false;
};

void ChordMemColumn::handleLeavingColumn(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                         KeyboardLayout* layout){};

void ChordMemColumn::handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, PressedPad pad,
                               KeyboardLayout* layout) {
	NotesState& currentNotesState = layout->getNotesState();

	if (pad.active) {
		activeChordMem = pad.y;
		auto noteCount = chordMemNoteCount[pad.y];
		for (int i = 0; i < noteCount && i < kMaxNotesChordMem; i++) {
			currentNotesState.enableNote(chordMem[pad.y][i], layout->velocity);
		}
		chordMemNoteCount[pad.y] = noteCount;
	}
	else {
		activeChordMem = 0xFF;
		if ((!chordMemNoteCount[pad.y] || Buttons::isShiftButtonPressed()) && currentNotesState.count) {
			auto noteCount = currentNotesState.count;
			for (int i = 0; i < noteCount && i < kMaxNotesChordMem; i++) {
				chordMem[pad.y][i] = currentNotesState.notes[i].note;
			}
			chordMemNoteCount[pad.y] = noteCount;
		}
		else if (Buttons::isShiftButtonPressed()) {
			chordMemNoteCount[pad.y] = 0;
		}
	}
};

void ChordMemColumn::writeToFile(Serializer& writer) {
	int num = 0;
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (chordMemNoteCount[y] > 0) {
			num = y + 1;
		}
	}

	if (num == 0) {
		return; // no state to save
	}

	writer.writeOpeningTag("chordMem");
	for (int32_t y = 0; y < num; y++) {
		writer.writeOpeningTag("chordSlot");
		for (int i = 0; i < chordMemNoteCount[y]; i++) {
			writer.writeOpeningTagBeginning("note");
			writer.writeAttribute("code", chordMem[y][i]);
			writer.closeTag();
		}
		writer.writeClosingTag("chordSlot");
	}
	writer.writeClosingTag("chordMem");
}

void ChordMemColumn::readFromFile(Deserializer& reader) {
	int slot_index = 0;
	const char* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "chordSlot")) {
			int y = slot_index++;
			if (y >= 8) {
				reader.exitTag("chordSlot");
				continue;
			}
			int i = 0;
			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "note")) {
					while (*(tagName = reader.readNextTagOrAttributeName())) {
						if (!strcmp(tagName, "code")) {
							if (i < kMaxNotesChordMem) {
								chordMem[y][i] = reader.readTagOrAttributeValueInt();
							}
						}
						else {
							reader.exitTag();
						}
					}
					i++;
				}
				else {
					reader.exitTag();
				}
			}
			chordMemNoteCount[y] = std::min(8, i);
		}
		else {
			reader.exitTag();
		}
	}
}

} // namespace deluge::gui::ui::keyboard::controls
