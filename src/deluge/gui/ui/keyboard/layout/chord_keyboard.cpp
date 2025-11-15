/*
 * Copyright © 2016-2024 Synthstrom Audible Limited
 * Copyright © 2024 Madeline Scyphers
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

#include "gui/ui/keyboard/layout/chord_keyboard.h"
#include "gui/colour/colour.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard/chords.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/functions.h"
#include <stdlib.h>

namespace deluge::gui::ui::keyboard::layout {

PLACE_SDRAM_TEXT void KeyboardLayoutChord::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Erase active notes
	KeyboardStateChord& state = getState().chord;

	// We run through the presses in reverse order to display the most recent pressed chord on top
	for (int32_t idxPress = kMaxNumKeyboardPadPresses - 1; idxPress >= 0; idxPress--) {

		PressedPad pressed = presses[idxPress];
		if (!pressed.active) {
			continue;
		}
		if (pressed.x < kChordKeyboardColumns) {
			if (mode == ChordKeyboardMode::ROW) {
				evaluatePadsRow(pressed);
			}
			else if (mode == ChordKeyboardMode::COLUMN) {
				evaluatePadsColumn(pressed);
			}
			else {
				D_PRINTLN("Invalid mode");
			}
		}
		else if (pressed.x < kDisplayWidth) {
			handleControlButton(pressed.x, pressed.y);
		}
	}
	ColumnControlsKeyboard::evaluatePads(presses);
	precalculate(); // Update chord quality colors if scale has changed
}

PLACE_SDRAM_TEXT void KeyboardLayoutChord::evaluatePadsRow(deluge::gui::ui::keyboard::PressedPad pressed) {
	KeyboardStateChord& state = getState().chord;
	NoteSet& scaleNotes = getScaleNotes();
	uint8_t scaleNoteCount = getScaleNoteCount();
	int32_t root = getRootNote() + state.noteOffset + state.modOffset;

	if (pressed.x < kChordKeyboardColumns - 1) {
		int32_t note = noteFromCoordsRow(pressed.x, pressed.y, root, scaleNotes, scaleNoteCount);
		drawChordName(note); // TODO: print full chord played on all pads?
		enableNote(note, velocity);
	}
	else if (pressed.x == kChordKeyboardColumns - 1) {
		for (int32_t i = 0; i < 3; ++i) {
			int32_t note = noteFromCoordsRow(i, pressed.y, root, scaleNotes, scaleNoteCount);
			if (i == 0) {
				drawChordName(note); // TODO: print quality
			}
			enableNote(note, velocity);
		}
	}
}

PLACE_SDRAM_TEXT void KeyboardLayoutChord::evaluatePadsColumn(PressedPad pressed) {
	KeyboardStateChord& state = getState().chord;

	NoteSet& scaleNotes = getScaleNotes();
	// We use the floor function to round down to the nearest octave instead of truncating
	int32_t octaveDisplacement = (int32_t)floor(float(pressed.x + state.scaleOffset) / scaleNotes.count());
	int32_t steps = scaleNotes[mod(pressed.x + state.scaleOffset, scaleNotes.count())];
	int32_t root = getRootNote() + state.noteOffset + steps;
	NoteSet scaleMode = scaleNotes.modulateByOffset(kOctaveSize - steps);
	ChordQuality quality = getChordQuality(scaleMode);
	auto chords = *chordColumns[static_cast<int>(quality)];
	Chord chord = chords[pressed.y % chords.size()];
	if (chord.name == kEmptyChord.name) {
		return;
	}

	Voicing voicing = chord.voicings[0];
	root = root + state.modOffset;
	drawChordName(root, chord.name, voicing.supplementalName);

	for (int32_t i = 0; i < kMaxChordKeyboardSize; i++) {
		int32_t offset = voicing.offsets[i];
		if (offset == NONE) {
			continue;
		}

		uint8_t note;
		if (state.autoVoiceLeading) {
			note = root + (offset + octaveDisplacement * kOctaveSize) % kOctaveSize;
		}
		else {
			note = root + offset + octaveDisplacement * kOctaveSize;
		}
		enableNote(note, velocity);
	}
}

PLACE_SDRAM_TEXT void KeyboardLayoutChord::handleVerticalEncoder(int32_t offset) {
	if (verticalEncoderHandledByColumns(offset)) {
		return;
	}
	offsetPads(offset, false);
}

PLACE_SDRAM_TEXT void KeyboardLayoutChord::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                                   PressedPad presses[kMaxNumKeyboardPadPresses],
                                                                   bool encoderPressed) {
	if (horizontalEncoderHandledByColumns(offset, shiftEnabled)) {
		return;
	}
	offsetPads(offset, shiftEnabled);
}

PLACE_SDRAM_TEXT void KeyboardLayoutChord::offsetPads(int32_t offset, bool shiftEnabled) {
	if (shiftEnabled) {
		if (mode == ChordKeyboardMode::ROW) {
			mode = ChordKeyboardMode::COLUMN;
		}
		else {
			mode = ChordKeyboardMode::ROW;
		}
		offset = 0;
	}
	KeyboardStateChord& state = getState().chord;
	state.scaleOffset += offset;
	precalculate();
}

PLACE_SDRAM_TEXT void KeyboardLayoutChord::precalculate() {
	KeyboardStateChord& state = getState().chord;

	Scale currentScale = currentSong->getCurrentScale();
	D_PRINTLN("Current scale: %d", currentScale);

	if (!(acceptedScales.find(currentScale) != acceptedScales.end())) {
		if (lastScale == NO_SCALE) {
			keyboardScreen.setScale(MAJOR_SCALE);
		}
		else {
			keyboardScreen.setScale(lastScale);
		}
		if (display->haveOLED()) {
			display->popupTextTemporary("Chord mode only supports modes of major and minor scales");
		}
		else {
			display->setScrollingText("SCALE NOT SUPPORTED", 0);
		}
	}
	else {
		lastScale = currentScale;
		NoteSet& scaleNotes = getScaleNotes();

		for (int32_t i = 0; i < qualities.size(); ++i) {
			// Since each row is an degree of our scale, if we modulate by the inverse of the scale note,
			//  we get the scale modes.
			NoteSet scaleMode = scaleNotes.modulateByOffset((kOctaveSize - scaleNotes[i % scaleNotes.count()]));
			ChordQuality chordQuality = getChordQuality(scaleMode);
			int32_t quality = static_cast<int32_t>(chordQuality);
			qualities[i] = quality;
		}
	}
}

PLACE_SDRAM_TEXT void KeyboardLayoutChord::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	KeyboardStateChord& state = getState().chord;
	// Iterate over grid image
	NoteSet& scaleNotes = getScaleNotes();
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; ++y) {
			if (x < kChordKeyboardColumns) {
				int32_t idx;
				if (mode == ChordKeyboardMode::ROW) {
					idx = y;
				}
				else {
					idx = x;
				}
				int32_t noteIdx = mod(idx + state.scaleOffset, scaleNotes.count());
				if (noteIdx == 0) {
					image[y][x] = qualityColours[qualities[noteIdx]];
				}
				else {
					image[y][x] = qualityColours[qualities[noteIdx]].forTail();
				}
				if (mode == ChordKeyboardMode::COLUMN) {
					int32_t quality = qualities[noteIdx];
					auto chords = *chordColumns[quality];
					Chord chord = chords[y % chords.size()];
					if (chord.name == kEmptyChord.name) {
						image[y][x] = colours::black;
					}
				}
			}
			else {
				image[y][x] = colours::black;
			}
			if ((x == kChordKeyboardColumns - 1) && (mode == ChordKeyboardMode::ROW)) {
				image[y][x] = colours::orange;
			}
		}
	}
	// TODO: Enable auto voice leading when it is implemented more fully
	// if (state.autoVoiceLeading) {
	// 	image[0][kDisplayWidth - 1] = colours::green;
	// }
	// else {
	// 	image[0][kDisplayWidth - 1] = colours::red;
	// }
	image[kDisplayHeight - 1][kDisplayWidth - 1] =
	    mode == ChordKeyboardMode::ROW ? colours::blue : colours::blue.forTail(); // Row mode
	image[kDisplayHeight - 2][kDisplayWidth - 1] =
	    mode == ChordKeyboardMode::COLUMN ? colours::purple : colours::purple.forTail(); // Column mode
}

PLACE_SDRAM_TEXT void KeyboardLayoutChord::handleControlButton(int32_t x, int32_t y) {
	KeyboardStateChord& state = getState().chord;
	// TODO: Enable auto voice leading when it is implemented more fully
	// if (x == kDisplayWidth - 1 && y == 0) {
	// 	state.autoVoiceLeading = !state.autoVoiceLeading;
	// 	if (state.autoVoiceLeading) {
	// 		char const* shortLong[2] = {"AUTO", "Auto Voice Leading: Beta"};
	// 		display->displayPopup(shortLong);
	// 	}
	// }
	if (x == kDisplayWidth - 1 && y == kDisplayHeight - 1) {
		mode = ChordKeyboardMode::ROW;
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_CHORD_KEYBOARD_MODE_ROW));
	}
	else if (x == kDisplayWidth - 1 && y == kDisplayHeight - 2) {
		mode = ChordKeyboardMode::COLUMN;
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_CHORD_KEYBOARD_MODE_COLUMN));
	}
}

PLACE_SDRAM_TEXT void KeyboardLayoutChord::drawChordName(int16_t noteCode, const char* chordName,
                                                         const char* voicingName) {
	char noteName[3] = {0};
	int32_t isNatural = 1; // gets modified inside noteCodeToString to be 0 if sharp.
	noteCodeToString(noteCode, noteName, &isNatural, false);

	char fullChordName[300];

	if (voicingName && *voicingName) {
		sprintf(fullChordName, "%s%s - %s", noteName, chordName, voicingName);
	}
	else {
		sprintf(fullChordName, "%s%s", noteName, chordName);
	}
	if (display->haveOLED()) {
		display->popupTextTemporary(fullChordName);
		D_PRINTLN("Popup text: %s", fullChordName);
	}
	else {
		int8_t drawDot = !isNatural ? 0 : 255;
		display->setScrollingText(fullChordName, 0);
	}
}

PLACE_SDRAM_TEXT uint8_t KeyboardLayoutChord::noteFromCoordsRow(int32_t x, int32_t y, int32_t root, NoteSet& scaleNotes,
                                                                uint8_t scaleNoteCount) {
	KeyboardStateChord& state = getState().chord;
	// We use the floor function to round down to the nearest octave instead of truncating
	int32_t octaveDisplacement =
	    state.autoVoiceLeading ? 0 : (int32_t)floor(float(y + scaleSteps[x] + state.scaleOffset) / scaleNoteCount);
	int32_t steps = scaleNotes[mod(y + scaleSteps[x] + state.scaleOffset, scaleNoteCount)];
	return root + steps + octaveDisplacement * kOctaveSize;
}

PLACE_SDRAM_TEXT bool KeyboardLayoutChord::allowSidebarType(ColumnControlFunction sidebarType) {
	if (sidebarType == ColumnControlFunction::CHORD) {
		return false;
	}
	return true;
}

} // namespace deluge::gui::ui::keyboard::layout
