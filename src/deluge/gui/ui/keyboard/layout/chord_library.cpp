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

#include "gui/ui/keyboard/layout/chord_library.h"
#include "gui/colour/colour.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard/chords.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "model/settings/runtime_feature_settings.h"
#include "processing/engines/audio_engine.h"
#include "util/functions.h"
#include <stdlib.h>

namespace deluge::gui::ui::keyboard::layout {

namespace {
// Suggestions used to pulse EVERY diatonic chord type at each suggested root — a flood, and it dragged
// in bland extensions that "don't sound good." We now only light the CORE chords: the basic triads and
// sevenths. At a diatonic root exactly one triad + one seventh fit the scale, so each suggested root
// shows just its two strongest options. Indices match the ChordList order in chords.cpp.
inline bool isCoreSuggestionChord(int32_t chordNo) {
	switch (chordNo) {
	case 1:  // kMajor
	case 2:  // kMinor
	case 8:  // k7
	case 11: // kM7
	case 12: // kMinor7
	case 15: // kDim
	case 17: // kAug
	case 20: // kMinor7b5
		return true;
	default:
		return false;
	}
}
} // namespace

void KeyboardLayoutChordLibrary::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Erase active notes
	KeyboardStateChordLibrary& state = getState().chordLibrary;

	// We run through the presses in reverse order to display the most recent pressed chord on top
	for (int32_t idxPress = kMaxNumKeyboardPadPresses - 1; idxPress >= 0; --idxPress) {

		PressedPad pressed = presses[idxPress];
		if (pressed.active && pressed.x < kDisplayWidth) {

			int32_t chordNo = getChordNo(pressed.y);

			Voicing voicing = state.chordList.getChordVoicing(chordNo);
			drawChordName(noteFromCoords(pressed.x), state.chordList.chords[chordNo].name, voicing.supplementalName);

			// resolveChordNotes() turns the selected chord+voicing into sounding notes. Capture for
			// placement is handled generically in KeyboardScreen from currentNotesState, so it works
			// here and in any other chord-producing mode.
			ChordSelection selection{.rootNote = noteFromCoords(pressed.x),
			                         .voicing = voicing,
			                         .chordNo = static_cast<int8_t>(chordNo),
			                         .velocity = static_cast<uint8_t>(velocity)};
			int16_t notes[kMaxChordKeyboardSize];
			uint8_t noteCount = resolveChordNotes(selection, notes, kMaxChordKeyboardSize);
			for (uint8_t i = 0; i < noteCount; i++) {
				enableNote(notes[i], velocity);
			}

			// Brain: remember the chord just pressed (home) and suggest the next-best roots to flash.
			if (getScaleModeEnabled()) {
				uint8_t rootPc = noteFromCoords(pressed.x) % kOctaveSize;
				homeRootPc = rootPc;
				homeChordNo = static_cast<int8_t>(chordNo);
				numSuggestions =
				    suggestNextChords(getRootNote(), getScaleNotes(), getScaleNoteCount(), rootPc, suggestions, 3);
			}
		}
	}
	ColumnControlsKeyboard::evaluatePads(presses);
}

void KeyboardLayoutChordLibrary::handleVerticalEncoder(int32_t offset) {
	if (verticalEncoderHandledByColumns(offset)) {
		return;
	}
	KeyboardStateChordLibrary& state = getState().chordLibrary;

	state.chordList.adjustChordRowOffset(offset);
	precalculate();
}

void KeyboardLayoutChordLibrary::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                         PressedPad presses[kMaxNumKeyboardPadPresses],
                                                         bool encoderPressed) {
	if (horizontalEncoderHandledByColumns(offset, shiftEnabled)) {
		return;
	}
	KeyboardStateChordLibrary& state = getState().chordLibrary;

	if (encoderPressed) {
		for (int32_t idxPress = kMaxNumKeyboardPadPresses - 1; idxPress >= 0; --idxPress) {

			PressedPad pressed = presses[idxPress];
			if (pressed.active && pressed.x < kDisplayWidth) {

				int32_t chordNo = getChordNo(pressed.y);

				state.chordList.adjustVoicingOffset(chordNo, offset);
			}
		}
	}
	else {
		state.noteOffset += offset;
	}
	precalculate();
}

void KeyboardLayoutChordLibrary::precalculate() {
	KeyboardStateChordLibrary& state = getState().chordLibrary;
	// Anchor the grid to the song's root note. On first entry we offset by the root; whenever the root
	// later changes (e.g. set in the piano roll), we shift the grid by the delta so it re-anchors to the
	// new root while preserving the user's horizontal scroll. (Previously this happened only once, so
	// changing the key left the grid stuck on the old/default root — it would "reanchor to C".)
	int16_t root = getRootNote();
	if (!initializedNoteOffset) {
		initializedNoteOffset = true;
		state.noteOffset += root;
		lastAnchoredRoot = root;
	}
	else if (lastAnchoredRoot != root) {
		state.noteOffset += (root - lastAnchoredRoot);
		lastAnchoredRoot = root;
	}

	// Pre-Buffer colours for next renderings
	for (int32_t i = 0; i < noteColours.size(); ++i) {
		noteColours[i] = getNoteColour(((state.noteOffset + i) % state.rowInterval) * state.rowColorMultiplier);
	}
	uint8_t hueStepSize = 192 / (kVerticalPages - 1); // 192 is the hue range for the rainbow
	for (int32_t i = 0; i < pageColours.size(); ++i) {
		pageColours[i] = getNoteColour(i * hueStepSize);
	}
}

void KeyboardLayoutChordLibrary::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	KeyboardStateChordLibrary& state = getState().chordLibrary;
	bool inScaleMode = getScaleModeEnabled();

	// Precreate list of all scale notes per octave
	NoteSet octaveScaleNotes;
	if (inScaleMode) {
		NoteSet& scaleNotes = getScaleNotes();
		for (uint8_t idx = 0; idx < getScaleNoteCount(); ++idx) {
			octaveScaleNotes.add(scaleNotes[idx]);
		}
	}

	// Iterate over grid image
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		int32_t noteCode = noteFromCoords(x);
		uint16_t noteWithinOctave = (uint16_t)((noteCode + kOctaveSize) - getRootNote()) % kOctaveSize;

		for (int32_t y = 0; y < kDisplayHeight; ++y) {
			int32_t chordNo = getChordNo(y);
			int32_t pageNo = chordNo / kDisplayHeight;

			if (inScaleMode) {
				NoteSet intervalSet = state.chordList.chords[chordNo].intervalSet;
				NoteSet modulatedNoteSet = intervalSet.modulateByOffset(noteWithinOctave);

				if (modulatedNoteSet.isSubsetOf(octaveScaleNotes)) {
					image[y][x] = noteColours[x % noteColours.size()];
				}
				else {
					image[y][x] = noteColours[x % noteColours.size()].dim(4);
				}
			}
			else {
				// show we've the last column, show the page colour for navigation
				if ((x == kDisplayWidth - 1) || (x == kDisplayWidth - 2)) {
					image[y][x] = pageColours[pageNo % pageColours.size()].dim(1);
				}
				// If not in scale mode, highlight the root note
				else if (noteWithinOctave == 0) {
					image[y][x] = noteColours[x % noteColours.size()];
				}
				else {
					image[y][x] = noteColours[x % noteColours.size()].dim(4);
				}
			}
		}
	}

	// Brain: pulse the harmonic options for where to go next. Every diatonic chord type at a suggested
	// root pulses WHITE (triads, 7ths, 9ths — whatever fits the scale); the chord you pressed (home)
	// pulses in its OWN colour, so you always know where you are. Pulse breathes ~0.75s to stand out.
	uint8_t phase = (AudioEngine::audioSampleTimer >> 7) & 0xFF;     // sawtooth, full cycle ~0.75s
	uint8_t tri = (phase < 128) ? (phase * 2) : ((255 - phase) * 2); // triangle 0..255..0
	uint8_t pulse = 30 + (uint8_t)((uint32_t)tri * 225 / 255);       // breathe between dim and full
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		uint8_t rootPc = (uint8_t)(noteFromCoords(x) % kOctaveSize);
		uint16_t noteWithinOctave = (uint16_t)((noteFromCoords(x) + kOctaveSize) - getRootNote()) % kOctaveSize;
		bool suggestedRoot = false;
		for (uint8_t s = 0; s < numSuggestions; s++) {
			if (suggestions[s].rootNote == rootPc) {
				suggestedRoot = true;
				break;
			}
		}
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			int32_t chordNo = getChordNo(y);
			// Home (the chord you pressed) pulses in its own colour so you know where you are.
			if (rootPc == homeRootPc && chordNo == homeChordNo) {
				image[y][x] = noteColours[x % noteColours.size()].adjust(pulse, 1);
			}
			// Only the core diatonic chords (triad + 7th) at a suggested root pulse white — not the whole
			// stack of extensions (that flooded the grid and surfaced bland chords).
			else if (suggestedRoot && inScaleMode && isCoreSuggestionChord(chordNo)) {
				NoteSet modulated = state.chordList.chords[chordNo].intervalSet.modulateByOffset(noteWithinOctave);
				if (modulated.isSubsetOf(octaveScaleNotes)) {
					image[y][x] = RGB::monochrome(pulse);
				}
			}
		}
	}
}

void KeyboardLayoutChordLibrary::drawChordName(int16_t noteCode, const char* chordName, const char* voicingName) {
	// Spell the root with flats or sharps to match the song's key (e.g. "Db" in F minor, not "C#").
	const char* noteName = noteNameInKey(((noteCode % 12) + 12) % 12, keyPrefersFlats(getRootNote(), getScaleNotes()));

	char fullChordName[300];

	if (voicingName && *voicingName) {
		sprintf(fullChordName, "%s%s - %s", noteName, chordName, voicingName);
	}
	else {
		sprintf(fullChordName, "%s%s", noteName, chordName);
	}

	if (display->haveOLED()) {
		display->popupTextTemporary(fullChordName);
	}
	else {
		display->setScrollingText(fullChordName, 0);
	}
}

bool KeyboardLayoutChordLibrary::allowSidebarType(ColumnControlFunction sidebarType) {
	if ((sidebarType == ColumnControlFunction::CHORD)) {
		return false;
	}
	return true;
}

} // namespace deluge::gui::ui::keyboard::layout
