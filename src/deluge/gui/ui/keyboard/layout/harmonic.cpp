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
#include "model/settings/runtime_feature_settings.h"
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

// One colour per scale degree, ordered so ADJACENT columns jump across the colour wheel (warm/cool
// interleaved) — never grouped, so neighbours always contrast hard. renderPads shades each light->dark.
const RGB kDegreeHue[7] = {
    RGB{.r = 255, .g = 0, .b = 0},   // i   red
    RGB{.r = 0, .g = 220, .b = 220}, // ii  cyan     (opposite red)
    RGB{.r = 235, .g = 220, .b = 0}, // III yellow
    RGB{.r = 45, .g = 65, .b = 255}, // iv  blue
    RGB{.r = 0, .g = 220, .b = 0},   // v   green
    RGB{.r = 235, .g = 0, .b = 235}, // VI  magenta
    RGB{.r = 255, .g = 120, .b = 0}, // VII orange
};

// Steep brightness ramp for the richness rows (bottom=triad brightest -> top=complex dimmest). Steep so
// the fade is obvious even across the bright lower rows, where a gentle ramp reads as flat on LEDs.
const uint8_t kRichBright[kDisplayHeight] = {255, 160, 102, 66, 45, 32, 24, 18};

const char* const kNumerals[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
const uint8_t kMajorIv[7] = {0, 2, 4, 5, 7, 9, 11};

// Brain flashes a suggested degree's triad (bottom) and its 7th — two fixed rows, so the basic next
// chords always show in the same place. (kLadder[0] = triad, kLadder[4] = "7".)
constexpr int32_t kRowTriad = 0;
constexpr int32_t kRow7th = 4;

// Divider control strip: THREE toggle buttons at the TOP; every dark pad below them acts as a CLEAR
// pad when tapped. The dark rows also visually separate the chord selector (left) from the keyboard.
constexpr int32_t kBtnIsoView = kDisplayHeight - 1; // toggle in-key <-> chromatic iso view
constexpr int32_t kBtnSticky = kDisplayHeight - 2;  // toggle sticky chord shape
constexpr int32_t kBtnBrain = kDisplayHeight - 3;   // toggle the next-chord brain on/off
constexpr int32_t kStripTopButtons = 3;             // lit toggle rows at the top; rows below = clear pads
// Bitmask of the dark (clear) rows = everything below the three toggles.
constexpr uint8_t kClearRowsMask = (uint8_t)((1u << (kDisplayHeight - kStripTopButtons)) - 1);

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
	// In-Key keyboard mapping (scale-step layout + colours). Anchor the panel ONE OCTAVE BELOW the chord
	// register (octaveBase) so the voicing — built at octaveBase — lands up in the middle of the panel
	// instead of jammed against the bottom edge (where the shape clips/flattens). Stable: the vertical
	// encoder moves octaveBase, so the panel scrolls with it but always keeps the chord framed.
	int32_t padIndex =
	    (getState().harmonic.octaveBase - 1) * (int32_t)sc + (x - kIsoStartCol) + y * getState().inKey.rowInterval;
	if (padIndex < 0) {
		padIndex = 0;
	}
	int32_t octave = padIndex / sc;
	int32_t idx = padIndex % sc;
	return octave * 12 + getRootNote() + getScaleNotes()[idx];
}

int32_t KeyboardLayoutHarmonic::isoNoteChromatic(int32_t x, int32_t y) {
	// Standard chromatic isomorphic mapping (semitone per column, rowInterval per row), anchored ONE
	// OCTAVE BELOW the chord register so the voicing lands in the middle of the panel, not at the bottom.
	return (getState().harmonic.octaveBase - 1) * 12 + getRootNote() + (x - kIsoStartCol)
	       + y * getState().isomorphic.rowInterval;
}

void KeyboardLayoutHarmonic::recomputeSuggestions(uint8_t keyRoot, const uint8_t* iv, uint8_t sc, uint8_t homeRootPc) {
	numSuggestions = 0;
	topDeg = -1;
	for (uint8_t d = 0; d < 7; d++) {
		degBright[d] = 0;
	}
	// Off when the brain is toggled off; otherwise diatonic-7-note only (like the Chord Library's brain).
	if (!getState().harmonic.brainOn || sc != 7 || !getScaleModeEnabled()) {
		return;
	}
	// Rank ALL diatonic next chords (best first), then shade each degree by its rank — strongest move
	// brightest, weakest dim-but-visible — so you see the whole map, not just a few picks.
	numSuggestions = (uint8_t)suggestNextChords(keyRoot, getScaleNotes(), sc, homeRootPc, suggestions, 7);
	for (uint8_t s = 0; s < numSuggestions; s++) {
		for (uint8_t d = 0; d < sc; d++) {
			if ((uint8_t)((keyRoot + iv[d]) % 12) == suggestions[s].rootNote) {
				degBright[d] = (numSuggestions > 1) ? (uint8_t)(235 - (uint32_t)s * 165 / (numSuggestions - 1)) : 235;
				if (s == 0) {
					topDeg = (int8_t)d;
				}
				break;
			}
		}
	}
	// The chord you're on (home) stays full so you always see where you are.
	for (uint8_t d = 0; d < sc; d++) {
		if ((uint8_t)((keyRoot + iv[d]) % 12) == homeRootPc) {
			degBright[d] = 255;
			break;
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

	uint8_t dividerNowMask = 0;
	bool isoPlayed = false;
	bool leftPicked = false;
	for (int32_t idx = kMaxNumKeyboardPadPresses - 1; idx >= 0; --idx) {
		PressedPad pressed = presses[idx];
		if (!pressed.active || pressed.x >= kDisplayWidth) {
			continue;
		}
		if (pressed.x == kDividerCol) {
			// The divider column is a 4-button control strip (handled by rising-edge after the loop).
			if (pressed.y >= 0 && pressed.y < kDisplayHeight) {
				dividerNowMask |= (uint8_t)(1u << pressed.y);
			}
			continue;
		}
		if (pressed.x >= kIsoStartCol) {
			// Right block: the iso panel — free play. Just sound the note; the selected chord highlight is
			// cleared afterwards (see below) so the grid resets out of "chord mode" and you can pick a new shape.
			isoPlayed = true;
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
			// Remember the EXACT voiced notes so the iso panel lights this one voicing (it may repeat up
			// the grid where the same notes recur, but never every octave).
			chordNoteCount = 0;
			for (uint8_t i = 0; i < n; i++) {
				enableNote((uint8_t)notes[i], velocity);
				chordNotes[chordNoteCount++] = notes[i];
			}
			heldCols |= (uint16_t)(1u << pressed.x);
			leftPicked = true;
			// Persist the selection so the white highlight + iso shape stay after release, and ask the
			// brain where to go next (suggested degree columns flash white in renderPads).
			selDeg = (int8_t)deg;
			selRichness =
			    (int8_t)((pressed.y < 0) ? 0 : (pressed.y >= kDisplayHeight ? kDisplayHeight - 1 : pressed.y));
			recomputeSuggestions(keyRoot, iv, sc, rootPc);
		}
	}

	KeyboardStateHarmonic& hs = getState().harmonic;

	auto clearSelection = [&]() {
		chordNoteCount = 0;
		selDeg = -1;
		selRichness = -1;
		numSuggestions = 0;
		topDeg = -1;
		for (uint8_t d = 0; d < 7; d++) {
			degBright[d] = 0;
		}
	};

	// Free play on the iso (without also picking a chord this frame) resets the grid out of "chord mode"
	// — UNLESS sticky is on, in which case the selected shape stays put while you play around it.
	if (isoPlayed && !leftPicked && !hs.stickyChord) {
		clearSelection();
	}

	// Divider control strip: three toggles at the top (rising-edge so holding doesn't repeat); tapping any
	// dark pad below them clears the selected chord + brain.
	uint8_t rising = (uint8_t)(dividerNowMask & ~dividerHeldMask);
	if (rising & (uint8_t)(1u << kBtnIsoView)) {
		hs.isoChromatic = !hs.isoChromatic;
		display->displayPopup(hs.isoChromatic ? "CHRO" : "KEY");
	}
	if (rising & (uint8_t)(1u << kBtnSticky)) {
		hs.stickyChord = !hs.stickyChord;
		display->displayPopup(hs.stickyChord ? "HOLD" : "FREE");
	}
	if (rising & (uint8_t)(1u << kBtnBrain)) {
		hs.brainOn = !hs.brainOn;
		if (!hs.brainOn) {
			numSuggestions = 0;
			topDeg = -1;
			for (uint8_t d = 0; d < 7; d++) {
				degBright[d] = 0;
			}
		}
		display->displayPopup(hs.brainOn ? "BRN" : "NOBR");
	}
	if (rising & kClearRowsMask) {
		clearSelection();
		display->displayPopup("CLR");
	}
	dividerHeldMask = dividerNowMask;

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

	// Match a pad's note against the EXACT voiced notes of the selected chord (not pitch classes), so the
	// voicing shows once where those notes land — it can repeat up the iso grid, but never every octave.
	auto inChordExact = [&](int32_t note) {
		for (uint8_t i = 0; i < chordNoteCount; i++) {
			if (chordNotes[i] == note) {
				return true;
			}
		}
		return false;
	};

	// The same voicing repeats up the iso grid, which is noisy. Pick ONE pad per voiced note — the lowest
	// (bottom-most, then left-most) occurrence — to draw at full white; the repeats render dim white, so a
	// single clean shape stays identifiable.
	bool primary[kDisplayHeight][kDisplayWidth] = {};
	for (uint8_t i = 0; i < chordNoteCount; i++) {
		bool placed = false;
		for (int32_t yy = 0; yy < kDisplayHeight && !placed; yy++) {
			for (int32_t xx = kIsoStartCol; xx < kDisplayWidth && !placed; xx++) {
				int32_t nn = chromatic ? isoNoteChromatic(xx, yy) : isoNoteAt(xx, yy);
				if (nn == chordNotes[i]) {
					primary[yy][xx] = true;
					placed = true;
				}
			}
		}
	}

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
			RGB hue = kDegreeHue[x % 7];
			bool haveBrain = (numSuggestions > 0); // a chord is selected and the brain is on
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				// Richness gradient: bottom (triad) bright -> top (complex) dim, on a perceptual ramp.
				RGB c = hue.adjustFractional(kRichBright[y], 255);
				if (x == selDeg && y == selRichness) {
					// Selected chord cell: a bright near-white tint of the column colour so you can see which
					// chord the iso panel is showing, while keeping the column's degree colour.
					c = RGB{.r = (uint8_t)((c.r + 255) >> 1),
					        .g = (uint8_t)((c.g + 255) >> 1),
					        .b = (uint8_t)((c.b + 255) >> 1)};
				}
				// Brain: like Library mode — each suggested degree flashes its triad + 7th cells white, with
				// brightness = how strong the move is (best brightest, weaker dimmer).
				else if (haveBrain && (y == kRowTriad || y == kRow7th) && x != selDeg && degBright[x] > 0) {
					c = RGB::monochrome((uint8_t)((uint32_t)pulse * degBright[x] / 255));
				}
				image[y][x] = c;
			}
		}
		else if (x == kDividerCol) {
			// Divider control strip: three toggles at the TOP, dark CLEAR pads below. The toggles are
			// achromatic (white = on/active, dim grey = off) — deliberately OFF the degree palette so they
			// never blend with the colourful chord columns; bright vs dim shows each toggle's state.
			bool sticky = getState().harmonic.stickyChord;
			bool brain = getState().harmonic.brainOn;
			constexpr uint8_t kOn = 245;
			constexpr uint8_t kOff = 75;
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				RGB c{};
				if (y == kBtnIsoView) {
					c = RGB::monochrome(chromatic ? kOn : kOff); // bright = chromatic, dim = in-key
				}
				else if (y == kBtnSticky) {
					c = RGB::monochrome(sticky ? kOn : kOff);
				}
				else if (y == kBtnBrain) {
					c = RGB::monochrome(brain ? kOn : kOff);
				}
				image[y][x] = c; // rows below the toggles stay dark (also the clear pads)
			}
		}
		else if (!chromatic) {
			// RIGHT (in-key): matches the In-Key keyboard. Dim coloured scale grid + steady coloured tonic;
			// the selected chord's EXACT voicing pops white; notes you play live and notes during playback
			// light just like the In-Key/Isomorphic keyboards (via the shared note-highlight buffer).
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
				uint8_t hi = getHighlightedNotes()[clamped];
				RGB nc = getNoteColour((uint8_t)clamped);
				if (inChordExact(note)) {
					// One instance (the lowest) full white = the identifiable shape; repeats dim white.
					image[y][x] = RGB::monochrome(primary[y][x] ? 255 : 65);
				}
				else if (playing) {
					image[y][x] = nc; // notes you play live light in their own colours
				}
				else if (hi != 0
				         && (hi >= 254
				             || runtimeFeatureSettings.get(RuntimeFeatureSettingType::HighlightIncomingNotes)
				                    == RuntimeFeatureStateToggle::On)) {
					// Playback note-preview (254 = dim white) + optional incoming-note highlight, like the
					// In-Key/Isomorphic keyboards — so the harmonic panel shows notes during playback.
					image[y][x] = (hi >= 254) ? RGB::monochrome(hi == 255 ? 255 : 110) : nc.adjust(hi, 1);
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
			// played notes full colour, off-scale dark; the selected chord's exact voicing pops white on top,
			// and playback notes light via the shared note-highlight buffer.
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
				uint8_t hi = getHighlightedNotes()[clamped];
				bool inScale = getScaleNotes().has(within);
				RGB nc = getNoteColour((uint8_t)clamped);
				if (inChordExact(note)) {
					image[y][x] = RGB::monochrome(primary[y][x] ? 255 : 65); // one bright shape, dim repeats
				}
				else if (playing || within == 0) {
					image[y][x] = nc; // root + live-played notes at full colour
				}
				else if (hi != 0
				         && (hi >= 254
				             || runtimeFeatureSettings.get(RuntimeFeatureSettingType::HighlightIncomingNotes)
				                    == RuntimeFeatureStateToggle::On)) {
					image[y][x] = (hi >= 254) ? RGB::monochrome(hi == 255 ? 255 : 110) : nc.adjust(hi, 1);
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
