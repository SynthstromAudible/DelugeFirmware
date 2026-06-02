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
#include "processing/engines/audio_engine.h"
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

int32_t KeyboardLayoutHarmonic::isoNoteChromatic(int32_t x, int32_t y) {
	// Standard chromatic isomorphic mapping (like the Isomorphic keyboard): semitone per column,
	// rowInterval semitones per row. Shares the Isomorphic layout's scroll/rowInterval so the toggled
	// view reads exactly like that keyboard. x is offset so the panel's left edge == iso col 0.
	return getState().isomorphic.scrollOffset + (x - kIsoStartCol) + y * getState().isomorphic.rowInterval;
}

void KeyboardLayoutHarmonic::recomputeSuggestions(uint8_t keyRoot, const uint8_t* iv, uint8_t sc, uint8_t homeRootPc) {
	suggestedDegMask = 0;
	numSuggestions = 0;
	// The brain is diatonic-7-note only (same constraint as the Chord Library's brain).
	if (sc != 7 || !getScaleModeEnabled()) {
		return;
	}
	numSuggestions = (uint8_t)suggestNextChords(keyRoot, getScaleNotes(), sc, homeRootPc, suggestions, 3);
	// Map each suggested root pitch-class back to its degree column so we can flash that whole column.
	for (uint8_t s = 0; s < numSuggestions; s++) {
		for (uint8_t d = 0; d < sc; d++) {
			if ((uint8_t)((keyRoot + iv[d]) % 12) == suggestions[s].rootNote) {
				suggestedDegMask |= (uint8_t)(1u << d);
				break;
			}
		}
	}
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

	bool dividerNow = false;
	for (int32_t idx = kMaxNumKeyboardPadPresses - 1; idx >= 0; --idx) {
		PressedPad pressed = presses[idx];
		if (!pressed.active || pressed.x >= kDisplayWidth) {
			continue;
		}
		if (pressed.x == kDividerCol) {
			// The divider column doubles as the iso-view toggle: one press flips in-key <-> chromatic.
			dividerNow = true;
			continue;
		}
		if (pressed.x >= kIsoStartCol) {
			// Right block: the iso panel. Play the note; KEEP the selected chord shape on screen (so you
			// can still see which shape you're studying) — played notes just light on top in their colours.
			int32_t note = getState().harmonic.isoChromatic ? isoNoteChromatic(pressed.x, pressed.y)
			                                                : isoNoteAt(pressed.x, pressed.y);
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
			// Persist the selection so the white highlight + iso shape stay after release, and ask the
			// brain where to go next (suggested degree columns flash white in renderPads).
			selDeg = (int8_t)deg;
			selRichness =
			    (int8_t)((pressed.y < 0) ? 0 : (pressed.y >= kDisplayHeight ? kDisplayHeight - 1 : pressed.y));
			recomputeSuggestions(keyRoot, iv, sc, rootPc);
		}
	}

	// Edge-detect the divider toggle so holding it doesn't flip every frame.
	if (dividerNow && !dividerHeld) {
		bool& chromatic = getState().harmonic.isoChromatic;
		chromatic = !chromatic;
		display->displayPopup(chromatic ? "CHRO" : "KEY");
	}
	dividerHeld = dividerNow;

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
	bool chromatic = getState().harmonic.isoChromatic;
	(void)iv;

	// Breathing white pulse for the brain's next-chord suggestions (same cadence as the Chord Library).
	uint8_t phase = (AudioEngine::audioSampleTimer >> 7) & 0xFF;     // sawtooth, full cycle ~0.75s
	uint8_t tri = (phase < 128) ? (phase * 2) : ((255 - phase) * 2); // triangle 0..255..0
	uint8_t pulse = 30 + (uint8_t)((uint32_t)tri * 225 / 255);       // breathe between dim and full

	for (int32_t x = 0; x < kDisplayWidth; x++) {
		if (x < kExplorerCols) {
			// LEFT: the chord explorer. Each column a distinct hue; the ROOT/triad (bottom) is lightest and
			// the column darkens upward as the chord grows lusher. The brain flashes suggested degree columns
			// white; the selected chord's cell is highlighted so you see which shape the iso panel is showing.
			if (x >= numCols) {
				for (int32_t y = 0; y < kDisplayHeight; y++) {
					image[y][x] = RGB{};
				}
				continue;
			}
			bool suggested = (suggestedDegMask >> x) & 1u;
			RGB hue = kDegreeHue[x % 7];
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				if (suggested) {
					image[y][x] = RGB::monochrome(pulse); // brain: flash this whole degree column white
					continue;
				}
				int32_t bright = 255 - y * 28; // bottom (root/triad) lightest -> darker up the richness ladder
				if (bright < 45) {
					bright = 45;
				}
				RGB c = hue.adjustFractional((uint16_t)bright, 255);
				if (x == selDeg && y == selRichness) {
					// Selected chord: a bright near-white tint of the column colour so it clearly reads as
					// "this is the chord you're looking at" while still showing which degree it belongs to.
					c = RGB{.r = (uint8_t)((c.r + 255) >> 1),
					        .g = (uint8_t)((c.g + 255) >> 1),
					        .b = (uint8_t)((c.b + 255) >> 1)};
				}
				image[y][x] = c;
			}
		}
		else if (x == kDividerCol) {
			// Divider / iso-view toggle. Dim so it's plainly not a play pad; tinted by the current mode
			// (warm white = in-key, cyan = chromatic) as a quiet status light.
			RGB tint = chromatic ? RGB{.r = 0, .g = 12, .b = 14} : RGB{.r = 12, .g = 12, .b = 12};
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				image[y][x] = tint;
			}
		}
		else if (!chromatic) {
			// RIGHT (in-key): matches the In-Key keyboard. Dim coloured scale grid, steady coloured tonic
			// anchor, the selected chord shape pops white, notes you play light in their own colours.
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
		else {
			// RIGHT (chromatic): matches the standard Isomorphic keyboard. Scale notes dim-coloured, root &
			// played notes full colour, off-scale dark; the selected chord shape still pops white on top.
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				int32_t note = isoNoteChromatic(x, y);
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
				bool inScale = getScaleNotes().has(within);
				RGB nc = getNoteColour((uint8_t)clamped);
				if (inChord) {
					image[y][x] = RGB::monochrome(255); // selected chord shape
				}
				else if (playing || within == 0) {
					image[y][x] = nc; // root + played notes at full colour
				}
				else if (inScale) {
					image[y][x] = nc.forTail(); // dim scale note, like the Isomorphic keyboard
				}
				else {
					image[y][x] = RGB::monochrome(0); // off-scale: dark
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
