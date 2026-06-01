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

#include "song_chord_mem.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/keyboard/layout/column_controls.h"
#include "hid/buttons.h"
#include "model/song/song.h"

namespace deluge::gui::ui::keyboard::controls {

// Brightness of the persistent chord-shape highlight on the grid (0-127, velocity-like).
constexpr uint8_t kChordMemHighlightBrightness = 90;

void SongChordMemColumn::renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column,
                                      KeyboardLayout* layout) {
	uint8_t otherChannels = 0;
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		bool chord_selected = y == activeChordMem;
		uint8_t chord_slot_filled = currentSong->chordMemNoteCount[y] > 0 ? 0x7f : 0;
		otherChannels = chord_selected ? 0xf0 : 0;
		uint8_t base = chord_selected ? 0xff : chord_slot_filled;
		image[y][column] = {otherChannels, base, base};
	}
}

bool SongChordMemColumn::handleVerticalEncoder(int8_t pad, int32_t offset) {
	return false;
};

void SongChordMemColumn::handleLeavingColumn(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                             KeyboardLayout* layout) {};

// Clear the currently-highlighted chord's notes from the grid highlight array.
void SongChordMemColumn::clearHighlight() {
	if (highlightSlot == 0xFF) {
		return;
	}
	for (int i = 0; i < currentSong->chordMemNoteCount[highlightSlot] && i < MAX_NOTES_CHORD_MEM; i++) {
		uint8_t note = currentSong->chordMem[highlightSlot][i];
		if (note < kHighestKeyboardNote) {
			keyboardScreen.highlightedNotes[note] = 0;
		}
	}
	highlightSlot = 0xFF;
	keyboardScreen.requestRendering();
}

// Persistently light a stored chord's notes on the grid so its shape stays visible after release.
void SongChordMemColumn::setHighlight(int32_t slot) {
	clearHighlight();
	for (int i = 0; i < currentSong->chordMemNoteCount[slot] && i < MAX_NOTES_CHORD_MEM; i++) {
		uint8_t note = currentSong->chordMem[slot][i];
		if (note < kHighestKeyboardNote) {
			keyboardScreen.highlightedNotes[note] = kChordMemHighlightBrightness;
		}
	}
	highlightSlot = slot;
	keyboardScreen.requestRendering();
}

void SongChordMemColumn::handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, PressedPad pad,
                                   KeyboardLayout* layout) {
	NotesState& currentNotesState = layout->getNotesState();

	if (pad.active) {
		activeChordMem = pad.y;
		auto noteCount = currentSong->chordMemNoteCount[pad.y];
		for (int i = 0; i < noteCount && i < MAX_NOTES_CHORD_MEM; i++) {
			currentNotesState.enableNote(currentSong->chordMem[pad.y][i], layout->velocity);
		}
		currentSong->chordMemNoteCount[pad.y] = noteCount;
		// Light the recalled chord's shape on the grid; it persists after release.
		if (noteCount > 0) {
			setHighlight(pad.y);
		}
	}
	else {
		activeChordMem = 0xFF;
		if ((!currentSong->chordMemNoteCount[pad.y] || Buttons::isShiftButtonPressed()) && currentNotesState.count) {
			// Slot contents about to change — drop its stale highlight first.
			if (highlightSlot == pad.y) {
				clearHighlight();
			}
			auto noteCount = currentNotesState.count;
			for (int i = 0; i < noteCount && i < MAX_NOTES_CHORD_MEM; i++) {
				currentSong->chordMem[pad.y][i] = currentNotesState.notes[i].note;
			}
			currentSong->chordMemNoteCount[pad.y] = noteCount;
		}
		else if (Buttons::isShiftButtonPressed()) {
			if (highlightSlot == pad.y) {
				clearHighlight();
			}
			currentSong->chordMemNoteCount[pad.y] = 0;
		}
	}
};

} // namespace deluge::gui::ui::keyboard::controls
