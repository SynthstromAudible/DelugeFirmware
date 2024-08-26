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

void ChordMemColumn::renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column, KeyboardLayout* layout) {
	uint8_t otherChannels = 0;
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		bool chord_selected = y == activeChordMem;
		uint8_t chord_slot_filled_1 = chordMemNoteCount[y] > 0 ? 0x7f : 0;
		uint8_t chord_slot_filled_2 = chordMemNoteCount[y] > 0 ? 0x3f : 0;
		otherChannels = chord_selected ? 0xf0 : 0;
		uint8_t base_1 = chord_selected ? 0xff : chord_slot_filled_1;
		uint8_t base_2 = chord_selected ? 0xff : chord_slot_filled_2;
		image[y][column] = {base_2, otherChannels, base_1};
	}
}

bool ChordMemColumn::handleVerticalEncoder(int8_t pad, int32_t offset) {
	return false;
};

void ChordMemColumn::handleLeavingColumn(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                         KeyboardLayout* layout) {};

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

	writer.writeArrayStart("chordMem", true, true);
	for (int32_t y = 0; y < num; y++) {
		writer.writeArrayStart("chordSlot", true, true);
		for (int i = 0; i < chordMemNoteCount[y]; i++) {
			writer.writeOpeningTagBeginning("note", true);
			writer.writeAttribute("code", chordMem[y][i]);
			writer.closeTag(true);
		}
		writer.writeArrayEnding("chordSlot", true, true);
	}
	writer.writeArrayEnding("chordMem", true, true);
}

// Need to match logic in the other chordMem handler.
void ChordMemColumn::readFromFile(Deserializer& reader) {
	reader.match('[');
	int slot_index = 0;
	const char* tagName;
	while (reader.match('{') && *(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "chordSlot")) {
			int y = slot_index++;
			if (y >= kDisplayHeight) {
				reader.exitTag("chordSlot");
				continue;
			}
			int i = 0;
			reader.match('[');
			while (reader.match('{') && *(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "note")) {
					reader.match('{');
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
					reader.match('}'); // note value object
					reader.match('}'); // note box
				}
				else {
					reader.exitTag();
				}
			}
			chordMemNoteCount[y] = std::min((int)kMaxNotesChordMem, i);
			reader.match(']'); // close note array.
			reader.match('}'); // close chordSlot box.
		}
		else {
			reader.exitTag();
		}
	}
	reader.match(']'); // close chordMem array.
}

} // namespace deluge::gui::ui::keyboard::controls
