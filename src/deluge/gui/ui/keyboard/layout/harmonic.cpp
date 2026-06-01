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

#include "gui/ui/keyboard/layout/harmonic.h"
#include "gui/colour/colour.h"
#include "gui/ui/keyboard/chords.h"
#include "hid/display/display.h"
#include <stdio.h>

namespace deluge::gui::ui::keyboard::layout {

namespace {

// Floor division / modulo so chord extensions (9/11/13) that reach above the octave map correctly.
inline int32_t floordiv(int32_t a, int32_t b) {
	int32_t q = a / b;
	if ((a % b != 0) && ((a < 0) != (b < 0))) {
		q--;
	}
	return q;
}
inline int32_t floormod(int32_t a, int32_t b) {
	int32_t r = a % b;
	if (r != 0 && ((r < 0) != (b < 0))) {
		r += b;
	}
	return r;
}

// The richness ladder: y = 0 (bottom) is a plain triad, climbing to y = 7 (top) for the lushest
// extension. `steps` are scale-degree offsets stacked from the column's degree (so every chord stays
// diatonic / in-key). `suffix` is appended to the Roman + absolute name. Ordering blessed by ear.
struct Richness {
	const char* suffix;
	int8_t steps[kMaxChordKeyboardSize];
	uint8_t count;
};
const Richness kLadder[kDisplayHeight] = {
    {"", {0, 2, 4, 0, 0, 0, 0}, 3},     // triad
    {"sus2", {0, 1, 4, 0, 0, 0, 0}, 3}, //
    {"sus4", {0, 3, 4, 0, 0, 0, 0}, 3}, //
    {"6", {0, 2, 4, 5, 0, 0, 0}, 4},    //
    {"7", {0, 2, 4, 6, 0, 0, 0}, 4},    //
    {"9", {0, 2, 4, 6, 8, 0, 0}, 5},    //
    {"11", {0, 2, 4, 6, 8, 10, 0}, 6},  //
    {"13", {0, 2, 4, 6, 8, 10, 12}, 7}, //
};

enum Function : uint8_t { FUNC_TONIC, FUNC_SUBDOM, FUNC_DOM };
// Diatonic functional grouping by scale-degree index — the same indices for major and minor:
// tonic = I/iii/vi (0,2,5), subdominant = ii/IV (1,3), dominant = V/vii (4,6).
const Function kDegreeFunction[7] = {FUNC_TONIC, FUNC_SUBDOM, FUNC_TONIC, FUNC_SUBDOM, FUNC_DOM, FUNC_TONIC, FUNC_DOM};
// Muted, desaturated hues (à la Chord Library) — renderPads dims them further per row. This is the
// "little brain": colour tells you each chord's role without ever dictating the next move.
const RGB kFunctionColour[3] = {
    RGB{.r = 0, .g = 130, .b = 60},  // tonic — green, rest
    RGB{.r = 35, .g = 80, .b = 175}, // subdominant — blue, motion
    RGB{.r = 175, .g = 50, .b = 30}, // dominant — red, tension
};

const char* const kNumerals[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
const uint8_t kMajorIv[7] = {0, 2, 4, 5, 7, 9, 11};

} // namespace

uint8_t KeyboardLayoutHarmonic::getScaleIntervals(uint8_t* ivOut) {
	NoteSet& scale = getScaleNotes();
	uint8_t count = 0;
	for (uint8_t i = 0; i < kOctaveSize && count < 12; i++) {
		if (scale.has(i)) {
			ivOut[count++] = i;
		}
	}
	return count;
}

uint8_t KeyboardLayoutHarmonic::buildChordAtDegree(uint8_t deg, int32_t y, const uint8_t* iv, uint8_t sc,
                                                   uint8_t keyRoot, int16_t* notesOut, uint8_t maxNotes,
                                                   uint8_t* rootPcOut, char* romanOut, char* absOut) {
	if (romanOut) {
		romanOut[0] = '\0';
	}
	if (absOut) {
		absOut[0] = '\0';
	}
	if (sc == 0) {
		return 0;
	}
	KeyboardStateHarmonic& state = getState().harmonic;
	int32_t anchor = state.octaveBase * 12 + keyRoot; // MIDI of the tonic
	uint8_t rootPc = (uint8_t)((keyRoot + iv[deg]) % 12);
	if (rootPcOut) {
		*rootPcOut = rootPc;
	}

	int32_t yc = (y < 0) ? 0 : (y >= kDisplayHeight ? (kDisplayHeight - 1) : y);
	const Richness& rich = kLadder[yc];
	uint8_t count = 0;
	for (uint8_t k = 0; k < rich.count && count < maxNotes; k++) {
		int32_t st = (int32_t)deg + rich.steps[k];
		int32_t midi = anchor + floordiv(st, sc) * 12 + iv[floormod(st, sc)];
		if (midi < 0) {
			midi = 0;
		}
		if (midi > 127) {
			midi = 127;
		}
		notesOut[count++] = (int16_t)midi;
	}

	// Names: absolute root spelled to the key, Roman from the degree + diatonic quality + richness suffix.
	bool flats = keyPrefersFlats(keyRoot, getScaleNotes());
	if (absOut) {
		sprintf(absOut, "%s%s", noteNameInKey(rootPc, flats), rich.suffix);
	}
	if (romanOut && sc == 7) {
		int8_t third = (int8_t)(((int32_t)iv[(deg + 2) % 7] - iv[deg] + 12) % 12);
		int8_t fifth = (int8_t)(((int32_t)iv[(deg + 4) % 7] - iv[deg] + 12) % 12);
		bool minorish = (third == 3);
		bool dim = (fifth == 6);
		bool aug = (fifth == 8);
		int8_t accidental = (int8_t)iv[deg] - (int8_t)kMajorIv[deg];
		char buf[32];
		int p = 0;
		for (int8_t a = 0; a < -accidental; a++) {
			buf[p++] = 'b';
		}
		for (int8_t a = 0; a < accidental; a++) {
			buf[p++] = '#';
		}
		for (const char* c = kNumerals[deg]; *c; c++) {
			buf[p++] = (minorish || dim) ? (char)(*c - 'A' + 'a') : *c;
		}
		if (dim) {
			buf[p++] = 'o';
		}
		else if (aug) {
			buf[p++] = '+';
		}
		buf[p] = '\0';
		sprintf(romanOut, "%s%s", buf, rich.suffix);
	}
	return count;
}

void KeyboardLayoutHarmonic::drawName(const char* roman, const char* abs) {
	char full[80];
	if (roman && roman[0]) {
		sprintf(full, "%s  %s", abs, roman);
	}
	else {
		sprintf(full, "%s", abs);
	}
	if (display->haveOLED()) {
		display->popupTextTemporary(full);
	}
	else {
		display->setScrollingText(full, 0);
	}
}

void KeyboardLayoutHarmonic::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Erase active notes
	uint8_t iv[12];
	uint8_t sc = getScaleIntervals(iv);
	uint8_t keyRoot = (uint8_t)getRootNote();
	uint8_t numCols = (sc > 7) ? 7 : sc;
	int32_t isoBase = getState().harmonic.octaveBase * 12 + keyRoot;
	heldCols = 0;

	// Reverse order so the most recently pressed chord wins the name display.
	for (int32_t idx = kMaxNumKeyboardPadPresses - 1; idx >= 0; --idx) {
		PressedPad pressed = presses[idx];
		if (!pressed.active || pressed.x >= kDisplayWidth) {
			continue;
		}
		if (pressed.x >= kExplorerCols) {
			// Right block: the isomorphic panel — play the note under the pad (noodle around the shape).
			int32_t note = isoNoteAt(pressed.x, pressed.y, isoBase);
			if (note >= 0 && note <= 127) {
				enableNote((uint8_t)note, velocity);
			}
			continue;
		}
		if (pressed.x < numCols) {
			// Left block: the harmonic explorer — columns are the diatonic degrees, in order.
			uint8_t deg = (uint8_t)pressed.x;
			int16_t notes[kMaxChordKeyboardSize];
			uint8_t rootPc = 0;
			char roman[32], abs[32];
			uint8_t n =
			    buildChordAtDegree(deg, pressed.y, iv, sc, keyRoot, notes, kMaxChordKeyboardSize, &rootPc, roman, abs);
			drawName(roman, abs);
			chordPcMask = 0;
			for (uint8_t i = 0; i < n; i++) {
				enableNote((uint8_t)notes[i], velocity);
				chordPcMask |= (uint16_t)(1u << (notes[i] % 12));
			}
			chordRootPc = (int8_t)rootPc; // selection drives the iso shape overlay
			heldCols |= (uint16_t)(1u << pressed.x);
		}
	}
	ColumnControlsKeyboard::evaluatePads(presses);
}

void KeyboardLayoutHarmonic::handleVerticalEncoder(int32_t offset) {
	if (verticalEncoderHandledByColumns(offset)) {
		return;
	}
	KeyboardStateHarmonic& state = getState().harmonic;
	state.octaveBase += offset;
	if (state.octaveBase < 1) {
		state.octaveBase = 1;
	}
	if (state.octaveBase > 8) {
		state.octaveBase = 8;
	}
	precalculate();
}

void KeyboardLayoutHarmonic::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                     PressedPad presses[kMaxNumKeyboardPadPresses],
                                                     bool encoderPressed) {
	// Single octave of fixed columns — nothing to scroll; just let the column controls have the encoder.
	horizontalEncoderHandledByColumns(offset, shiftEnabled);
}

void KeyboardLayoutHarmonic::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	uint8_t iv[12];
	uint8_t sc = getScaleIntervals(iv);
	bool seven = (sc == 7) && getScaleModeEnabled();
	uint8_t keyRoot = (uint8_t)getRootNote();
	uint8_t numCols = (sc > 7) ? 7 : sc;
	KeyboardStateHarmonic& state = getState().harmonic;
	int32_t isoBase = state.octaveBase * 12 + keyRoot;

	for (int32_t x = 0; x < kDisplayWidth; x++) {
		if (x < kExplorerCols) {
			// LEFT: the harmonic explorer — the 7 in-key chords, function-coloured.
			if (x >= numCols) {
				for (int32_t y = 0; y < kDisplayHeight; y++) {
					image[y][x] = RGB{}; // unused explorer columns stay dark
				}
				continue;
			}
			uint8_t deg = (uint8_t)x;
			uint8_t rootPc = (uint8_t)((keyRoot + iv[deg]) % 12);
			// Function colour for 7-note scales (the "little brain"); note-colour fallback otherwise.
			RGB colCol = seven ? kFunctionColour[kDegreeFunction[deg]]
			                   : getNoteColour((uint8_t)(state.octaveBase * 12 + rootPc));
			bool held = (heldCols >> x) & 1u;
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				// Calm dim map (richer rows a touch brighter); the held column lights up as feedback.
				uint16_t bright = held ? (uint16_t)(200 + y * 4) : (uint16_t)(70 + y * 6);
				if (bright > 255) {
					bright = 255;
				}
				image[y][x] = colCol.adjustFractional(bright, 255);
			}
		}
		else {
			// RIGHT: the isomorphic visualiser — the selected chord lit as a shape over dim wallpaper.
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				int32_t note = isoNoteAt(x, y, isoBase);
				int32_t clamped = (note < 0) ? 0 : (note > 127 ? 127 : note);
				uint8_t pc = (uint8_t)(((clamped % 12) + 12) % 12);
				RGB base = getNoteColour((uint8_t)clamped);
				RGB cell;
				if (chordRootPc >= 0 && pc == (uint8_t)chordRootPc) {
					cell = RGB::monochrome(230); // root = bright white anchor
				}
				else if (chordPcMask & (uint16_t)(1u << pc)) {
					cell = base.adjustFractional(205, 255); // chord tone = bright note colour
				}
				else {
					cell = base.adjustFractional(45, 255); // wallpaper = dim
				}
				image[y][x] = cell;
			}
		}
	}
}

bool KeyboardLayoutHarmonic::allowSidebarType(ColumnControlFunction sidebarType) {
	if (sidebarType == ColumnControlFunction::CHORD) {
		return false;
	}
	return true;
}

} // namespace deluge::gui::ui::keyboard::layout
