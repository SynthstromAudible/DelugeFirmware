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

// Richness ladder: y=0 (bottom) plain triad, climbing to y=7 (top) for the lushest extension. `steps`
// are scale-degree offsets stacked from the column's degree (always diatonic). `suffix` -> name.
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

// One DISTINCT colour per scale degree for clear column contrast (renderPads shades each light->dark).
const RGB kDegreeHue[7] = {
    RGB{.r = 225, .g = 40, .b = 40},  // i   red
    RGB{.r = 225, .g = 120, .b = 25}, // ii  orange
    RGB{.r = 205, .g = 195, .b = 30}, // III yellow
    RGB{.r = 45, .g = 200, .b = 70},  // iv  green
    RGB{.r = 30, .g = 190, .b = 205}, // v   cyan
    RGB{.r = 60, .g = 95, .b = 230},  // VI  blue
    RGB{.r = 165, .g = 65, .b = 220}, // VII purple
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

int32_t KeyboardLayoutHarmonic::isoNoteAt(int32_t x, int32_t y) {
	uint8_t sc = getScaleNoteCount();
	if (sc == 0) {
		return getRootNote();
	}
	// EXACTLY the In-Key keyboard's mapping (same scrollOffset + rowInterval + scale-step->note formula),
	// so the panel lays out identically to In-Key mode. x is offset so the panel's left edge == In-Key col 0.
	int32_t padIndex = getState().inKey.scrollOffset + (x - kIsoStartCol) + y * getState().inKey.rowInterval;
	if (padIndex < 0) {
		padIndex = 0;
	}
	int32_t octave = padIndex / sc;
	int32_t idx = padIndex % sc;
	return octave * 12 + getRootNote() + getScaleNotes()[idx];
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
	int32_t anchor = getState().harmonic.octaveBase * 12 + keyRoot;
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
	heldCols = 0;

	for (int32_t idx = kMaxNumKeyboardPadPresses - 1; idx >= 0; --idx) {
		PressedPad pressed = presses[idx];
		if (!pressed.active || pressed.x >= kDisplayWidth) {
			continue;
		}
		if (pressed.x >= kIsoStartCol) {
			// Right block: the iso panel — playing it clears the chord overlay so you can free-play clean.
			chordPcMask = 0;
			int32_t note = isoNoteAt(pressed.x, pressed.y);
			if (note >= 0 && note <= 127) {
				enableNote((uint8_t)note, velocity);
			}
			continue;
		}
		if (pressed.x < numCols) {
			// Left block: the chord explorer — columns are the diatonic degrees, in order.
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
				chordPcMask |= (uint16_t)(1u << (notes[i] % 12)); // remember the chord's pitch-classes
			}
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
	horizontalEncoderHandledByColumns(offset, shiftEnabled);
}

void KeyboardLayoutHarmonic::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	uint8_t iv[12];
	uint8_t sc = getScaleIntervals(iv);
	uint8_t keyRoot = (uint8_t)getRootNote();
	uint8_t numCols = (sc > 7) ? 7 : sc;
	(void)iv;

	for (int32_t x = 0; x < kDisplayWidth; x++) {
		if (x < kExplorerCols) {
			// LEFT: the chord explorer — each column a distinct hue, shading light->dark up the rows.
			if (x >= numCols) {
				for (int32_t y = 0; y < kDisplayHeight; y++) {
					image[y][x] = RGB{};
				}
				continue;
			}
			RGB hue = kDegreeHue[x % 7];
			bool held = (heldCols >> x) & 1u;
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				int32_t bright = held ? 255 : (255 - y * 30); // triad full -> 13 dim: a clear gradient up
				if (bright < 35) {
					bright = 35;
				}
				image[y][x] = hue.adjustFractional((uint16_t)bright, 255);
			}
		}
		else if (x < kIsoStartCol) {
			// Blank divider column.
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				image[y][x] = RGB{};
			}
		}
		else {
			// RIGHT: in-key iso. Coloured scale grid (dim), bright coloured tonic anchor, the voiced chord
			// pops white, and notes you play light in their own colours.
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				int32_t note = isoNoteAt(x, y);
				int32_t clamped = (note < 0) ? 0 : (note > 127 ? 127 : note);
				uint8_t pc = (uint8_t)(((clamped % 12) + 12) % 12);
				uint8_t within = (uint8_t)(((pc + kOctaveSize) - keyRoot) % kOctaveSize); // 0 = scale tonic
				bool playing = false;
				for (uint8_t i = 0; i < currentNotesState.count; i++) {
					if (currentNotesState.notes[i].note == note) {
						playing = true;
						break;
					}
				}
				bool inChord = (chordPcMask & (uint16_t)(1u << pc)) != 0;
				RGB nc = getNoteColour((uint8_t)clamped);
				if (playing) {
					image[y][x] = nc; // play notes light in their colours, like a normal keyboard
				}
				else if (inChord) {
					image[y][x] = RGB::monochrome(255); // the selected chord pops bright white
				}
				else if (within == 0) {
					image[y][x] = nc; // bright COLOURED tonic anchor (steady, follows the key)
				}
				else {
					image[y][x] = nc.dim(4); // quiet dim coloured in-key grid, so chord/tonic stand out
				}
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
