/*
 * Copyright © 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#include "gui/ui/keyboard/layout/expressive_chords.h"
#include <algorithm>

#include "gui/colour/colour.h"
#include "gui/ui/keyboard/expressive_chord_sets.h"
#include "hid/display/display.h"
#include "model/scale/preset_scales.h"
#include "util/const_functions.h"
#include "util/functions.h"

namespace deluge::gui::ui::keyboard::layout {

namespace {

Scale scaleFromRootChord(const Chord* chord) {
	NoteSet intervals = chord->intervalSet;
	switch (getChordQuality(intervals)) {
	case ChordQuality::MINOR:
		return MINOR_SCALE;
	case ChordQuality::DIMINISHED:
		return LOCRIAN_SCALE;
	case ChordQuality::DOMINANT:
		return MIXOLYDIAN_SCALE;
	case ChordQuality::AUGMENTED:
		return LYDIAN_SCALE;
	case ChordQuality::MAJOR:
	case ChordQuality::OTHER:
	default:
		return MAJOR_SCALE;
	}
}

int32_t snapSemitoneToScale(int32_t semitoneOffset, NoteSet scale) {
	semitoneOffset = mod(semitoneOffset, kOctaveSize);
	if (scale.has(semitoneOffset)) {
		return semitoneOffset;
	}
	for (int32_t d = 1; d < kOctaveSize; d++) {
		int32_t up = mod(semitoneOffset + d, kOctaveSize);
		int32_t down = mod(semitoneOffset - d, kOctaveSize);
		if (scale.has(up)) {
			return up;
		}
		if (scale.has(down)) {
			return down;
		}
	}
	return semitoneOffset;
}

} // namespace

int32_t KeyboardLayoutExpressiveChords::padToChordIndex(int32_t x, int32_t y) {
	int32_t localX = x - kExpressiveChordOriginX;
	int32_t localY = y - kExpressiveChordOriginY;
	if (localX < 0 || localX >= kExpressiveChordColumns || localY < 0 || localY >= kExpressiveChordRows) {
		return -1;
	}
	return localY * kExpressiveChordColumns + localX;
}

bool KeyboardLayoutExpressiveChords::isSetControlPad(int32_t x, int32_t y, int32_t* setOffset) {
	// Column 15: bottom row = next set, top row = previous set.
	if (x == kDisplayWidth - 1 && y == 0) {
		*setOffset = 1;
		return true;
	}
	if (x == kDisplayWidth - 1 && y == kDisplayHeight - 1) {
		*setOffset = -1;
		return true;
	}
	return false;
}

bool KeyboardLayoutExpressiveChords::isInversionPad(int32_t x, int32_t y, int32_t* inversionOut) {
	if (x != kExpressiveInversionColumnX) {
		return false;
	}
	*inversionOut = y;
	return true;
}

bool KeyboardLayoutExpressiveChords::isSpreadPad(int32_t x, int32_t y, int32_t* spreadOut) {
	if (x != kExpressiveSpreadColumnX) {
		return false;
	}
	*spreadOut = y;
	return true;
}

int32_t KeyboardLayoutExpressiveChords::computeSlotRoot(int32_t chordIndex, const ExpressiveChordSet& set) {
	KeyboardStateExpressiveChords& state = getState().expressiveChords;
	const ExpressiveChordSlot& slot0 = set.slots[kExpressiveRootChordIndex];
	int32_t refRoot = slot0.rootMidi + state.transpose;

	if (chordIndex == kExpressiveRootChordIndex) {
		return refRoot;
	}

	const ExpressiveChordSlot& slot = set.slots[chordIndex];
	NoteSet scale = presetScaleNotes[scaleFromRootChord(slot0.chord)];

	int32_t relSemitones = slot.rootMidi - slot0.rootMidi;
	int32_t relPc = mod(relSemitones, kOctaveSize);
	int32_t snappedRel = snapSemitoneToScale(relPc, scale);
	return refRoot + snappedRel + (relSemitones - relPc);
}

bool KeyboardLayoutExpressiveChords::slotRootInHarmonicScale(int32_t chordIndex, const ExpressiveChordSet& set) {
	if (chordIndex == kExpressiveRootChordIndex) {
		return true;
	}
	const ExpressiveChordSlot& slot0 = set.slots[kExpressiveRootChordIndex];
	NoteSet scale = presetScaleNotes[scaleFromRootChord(slot0.chord)];
	int32_t relPc = mod(set.slots[chordIndex].rootMidi - slot0.rootMidi, kOctaveSize);
	return scale.has(relPc);
}

void KeyboardLayoutExpressiveChords::changeChordSet(int32_t offset) {
	KeyboardStateExpressiveChords& state = getState().expressiveChords;
	int32_t next = static_cast<int32_t>(state.setIndex) + offset;
	if (next < 0) {
		next = kExpressiveChordSetCount - 1;
	}
	else if (next >= static_cast<int32_t>(kExpressiveChordSetCount)) {
		next = 0;
	}
	state.setIndex = static_cast<uint8_t>(next);
	display->displayPopup(expressiveChordSets[state.setIndex].name);
	precalculate();
}

Voicing KeyboardLayoutExpressiveChords::voicingForSlot(const ExpressiveChordSlot& slot) {
	for (int8_t idx = std::min<int8_t>(slot.voicingIndex, kUniqueVoicings - 1); idx >= 0; idx--) {
		Voicing voicing = slot.chord->voicings[idx];
		for (int32_t i = 0; i < kMaxChordKeyboardSize; i++) {
			if (voicing.offsets[i] != 0) {
				return voicing;
			}
		}
	}
	return slot.chord->voicings[0];
}

int32_t KeyboardLayoutExpressiveChords::buildChordNotes(int32_t chordIndex, int32_t outNotes[kMaxChordKeyboardSize]) {
	KeyboardStateExpressiveChords& state = getState().expressiveChords;
	const ExpressiveChordSet& set = expressiveChordSets[state.setIndex % kExpressiveChordSetCount];
	const ExpressiveChordSlot& slot = set.slots[chordIndex];

	if (!slot.chord || slot.chord->name[0] == '\0') {
		return 0;
	}

	Voicing voicing = voicingForSlot(slot);
	int32_t root = computeSlotRoot(chordIndex, set);

	int32_t notes[kMaxChordKeyboardSize];
	int32_t count = 0;
	for (int32_t i = 0; i < kMaxChordKeyboardSize; i++) {
		if (voicing.offsets[i] == NONE) {
			continue;
		}
		notes[count++] = root + voicing.offsets[i];
	}
	if (count == 0) {
		return 0;
	}

	for (int32_t i = 0; i < count - 1; i++) {
		for (int32_t j = i + 1; j < count; j++) {
			if (notes[i] > notes[j]) {
				int32_t tmp = notes[i];
				notes[i] = notes[j];
				notes[j] = tmp;
			}
		}
	}

	uint8_t inversionSteps = state.inversion % count;
	for (uint8_t step = 0; step < inversionSteps; step++) {
		int32_t bass = notes[0];
		for (int32_t i = 0; i < count - 1; i++) {
			notes[i] = notes[i + 1];
		}
		notes[count - 1] = bass + kOctaveSize;
	}

	if (state.spread > 0 && count > 1) {
		for (int32_t i = 1; i < count; i++) {
			int32_t octavesUp = (static_cast<int32_t>(state.spread) * i) / (count - 1);
			notes[i] += octavesUp * kOctaveSize;
		}
	}

	for (int32_t i = 0; i < count; i++) {
		outNotes[i] = notes[i];
	}
	return count;
}

void KeyboardLayoutExpressiveChords::setInversion(uint8_t inversion) {
	getState().expressiveChords.inversion = inversion;
}

void KeyboardLayoutExpressiveChords::setSpread(uint8_t spread) {
	getState().expressiveChords.spread = spread;
}

void KeyboardLayoutExpressiveChords::changeChordSetByOffset(int32_t offset) {
	changeChordSet(offset);
}

void KeyboardLayoutExpressiveChords::adjustTranspose(int32_t offset) {
	getState().expressiveChords.transpose += offset;
	refreshSlotColours();
}

void KeyboardLayoutExpressiveChords::refreshSlotColours() {
	precalculate();
}

RGB KeyboardLayoutExpressiveChords::slotColour(int32_t chordIndex) const {
	if (chordIndex < 0 || chordIndex >= kExpressiveChordsPerSet) {
		return colours::black;
	}
	return slotColours[chordIndex];
}

void KeyboardLayoutExpressiveChords::playSlotIntoNotesState(int32_t chordIndex, uint8_t vel) {
	int32_t notes[kMaxChordKeyboardSize];
	int32_t count = buildChordNotes(chordIndex, notes);
	for (int32_t i = 0; i < count; i++) {
		enableNote(notes[i], vel);
	}
}

void KeyboardLayoutExpressiveChords::playSlot(int32_t chordIndex, uint8_t vel) {
	const ExpressiveChordSet& set =
	    expressiveChordSets[getState().expressiveChords.setIndex % kExpressiveChordSetCount];
	const ExpressiveChordSlot& slot = set.slots[chordIndex];

	if (!slot.chord || slot.chord->name[0] == '\0') {
		return;
	}

	int32_t root = computeSlotRoot(chordIndex, set);
	Voicing voicing = voicingForSlot(slot);
	drawChordName(root, slot.chord->name, voicing.supplementalName);

	int32_t notes[kMaxChordKeyboardSize];
	int32_t count = buildChordNotes(chordIndex, notes);
	for (int32_t i = 0; i < count; i++) {
		enableNote(notes[i], vel);
	}
}

void KeyboardLayoutExpressiveChords::appendMidiHeldSlots() {
	KeyboardStateExpressiveChords& state = getState().expressiveChords;
	for (int32_t i = 0; i < kExpressiveChordsPerSet; i++) {
		if (state.midiHeldMask & (1UL << i)) {
			playSlotIntoNotesState(i, state.midiVelocity[i]);
		}
	}
}

void KeyboardLayoutExpressiveChords::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{};
	KeyboardStateExpressiveChords& state = getState().expressiveChords;

	for (int32_t idxPress = kMaxNumKeyboardPadPresses - 1; idxPress >= 0; --idxPress) {
		PressedPad pressed = presses[idxPress];
		if (!pressed.active || pressed.x >= kDisplayWidth) {
			continue;
		}
		int32_t setOffset = 0;
		if (isSetControlPad(pressed.x, pressed.y, &setOffset) && !pressed.padPressHeld) {
			changeChordSet(setOffset);
			continue;
		}
		int32_t inversion = 0;
		if (isInversionPad(pressed.x, pressed.y, &inversion) && !pressed.padPressHeld) {
			state.inversion = static_cast<uint8_t>(inversion);
			char text[12];
			sprintf(text, "Inv %d", inversion);
			display->displayPopup(text);
			continue;
		}
		int32_t spread = 0;
		if (isSpreadPad(pressed.x, pressed.y, &spread) && !pressed.padPressHeld) {
			state.spread = static_cast<uint8_t>(spread);
			char text[12];
			sprintf(text, "Spread %d", spread);
			display->displayPopup(text);
			continue;
		}
		int32_t chordIndex = padToChordIndex(pressed.x, pressed.y);
		if (chordIndex >= 0) {
			playSlot(chordIndex, velocity);
		}
	}
	ColumnControlsKeyboard::evaluatePads(presses);
}

void KeyboardLayoutExpressiveChords::handleVerticalEncoder(int32_t offset) {
	if (verticalEncoderHandledByColumns(offset)) {
		return;
	}
	changeChordSet(offset);
}

void KeyboardLayoutExpressiveChords::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                             PressedPad presses[kMaxNumKeyboardPadPresses],
                                                             bool encoderPressed) {
	if (horizontalEncoderHandledByColumns(offset, shiftEnabled)) {
		return;
	}
	KeyboardStateExpressiveChords& state = getState().expressiveChords;
	state.transpose += offset;
	precalculate();
}

void KeyboardLayoutExpressiveChords::precalculate() {
	KeyboardStateExpressiveChords& state = getState().expressiveChords;
	const ExpressiveChordSet& set = expressiveChordSets[state.setIndex % kExpressiveChordSetCount];
	for (int32_t i = 0; i < kExpressiveChordsPerSet; ++i) {
		NoteSet intervals = set.slots[i].chord->intervalSet;
		ChordQuality q = getChordQuality(intervals);
		int32_t qi = static_cast<int32_t>(q);
		if (qi >= static_cast<int32_t>(ChordQuality::CHORD_QUALITY_MAX)) {
			qi = 0;
		}
		static const RGB qualityColours[] = {colours::blue,  colours::purple,
		                                     colours::green, colours::kelly::very_light_blue,
		                                     colours::cyan,  colours::yellow};
		RGB colour = qualityColours[qi];
		if (i == kExpressiveRootChordIndex) {
			colour = colour.dim(1);
		}
		else {
			colour = colour.dim(1 + (i % 3));
			if (!slotRootInHarmonicScale(i, set)) {
				colour = colour.dim(4);
			}
		}
		slotColours[i] = colour;
	}
	(void)state;
}

void KeyboardLayoutExpressiveChords::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	KeyboardStateExpressiveChords& state = getState().expressiveChords;

	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		for (int32_t x = 0; x < kDisplayWidth; ++x) {
			int32_t idx = padToChordIndex(x, y);
			if (idx >= 0) {
				image[y][x] = slotColours[idx];
			}
			else if (x == kExpressiveInversionColumnX) {
				image[y][x] = (y == state.inversion) ? colours::cyan : colours::cyan.dim(3 + y);
			}
			else if (x == kExpressiveSpreadColumnX) {
				image[y][x] = (y == state.spread) ? colours::green : colours::green.dim(3 + y);
			}
			else {
				image[y][x] = colours::black;
			}
		}
	}
	image[0][kDisplayWidth - 1] = colours::magenta;
	image[kDisplayHeight - 1][kDisplayWidth - 1] = colours::magenta.forTail();
}

void KeyboardLayoutExpressiveChords::drawChordName(int16_t noteCode, const char* chordName, const char* voicingName) {
	char noteName[3] = {0};
	int32_t isNatural = 1;
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
	}
	else {
		display->setScrollingText(fullChordName, 0);
	}
}

bool KeyboardLayoutExpressiveChords::allowSidebarType(ColumnControlFunction sidebarType) {
	return sidebarType != ColumnControlFunction::CHORD;
}

} // namespace deluge::gui::ui::keyboard::layout
