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

#include "definitions.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard/layout/isomorphic.h"
#include "gui/ui/sound_editor.h"
#include "hid/buttons.h"
#include "model/instrument/melodic_instrument.h"
#include "model/model_stack.h"
#include "model/settings/runtime_feature_settings.h"
#include "storage/flash_storage.h"
#include "util/functions.h"
#include <limits>

#define LEFT_COL kDisplayWidth
#define RIGHT_COL (kDisplayWidth + 1)
#define MAX_CHORD_NOTES 4

namespace deluge::gui::ui::keyboard::layout {

uint8_t chordTypeSemitoneOffsets[][MAX_CHORD_NOTES] = {
    /* NO_CHORD  */ {0, 0, 0, 0},
    /* FIFTH     */ {7, 0, 0, 0},
    /* SUS2      */ {2, 7, 0, 0},
    /* MINOR     */ {3, 7, 0, 0},
    /* MAJOR     */ {4, 7, 0, 0},
    /* SUS4      */ {5, 7, 0, 0},
    /* MINOR7    */ {3, 7, 10, 0},
    /* DOMINANT7 */ {4, 7, 10, 0},
    /* MAJOR7    */ {4, 7, 11, 0},
};

const char* chordNames[] = {
    /* NO_CHORD  */ "NONE",
    /* FIFTH     */ "5TH",
    /* SUS2      */ "SUS2",
    /* MINOR     */ "MIN",
    /* MAJOR     */ "MAJ",
    /* SUS4      */ "SUS4",
    /* MINOR7    */ "MIN7",
    /* DOMINANT7 */ "DOM7",
    /* MAJOR7    */ "MAJ7",
};

const char* functionNames[][2] = {
    /* VELOCITY    */ {"VEL", "Velocity"},
    /* MOD         */ {"MOD", "Modwheel"},
    /* CHORD       */ {"CHRD", "Chords"},
    /* CHORD_MEM   */ {"CMEM", "Chord Memory"},
    /* SCALE_MODE  */ {"SMOD", "Scales"},
    /* BEAT_REPEAT */ {"BEAT", "Beat Repeat"},
};

void ColumnControlsKeyboard::enableNote(uint8_t note, uint8_t velocity) {
	currentNotesState.enableNote(note, velocity);
	for (int i = 0; i < MAX_CHORD_NOTES; i++) {
		auto offset = chordSemitoneOffsets[i];
		if (!offset) {
			break;
		}
		currentNotesState.enableNote(note + offset, velocity, true);
	}
}

void ColumnControlsKeyboard::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(getCurrentClip());

	leftColHeld = -1;

	rightColHeld = -1;

	for (int32_t idxPress = 0; idxPress < kMaxNumKeyboardPadPresses; ++idxPress) {
		auto pressed = presses[idxPress];
		// in note columns
		if (pressed.x == LEFT_COL) {
			if (pressed.active) {
				leftColHeld = pressed.y;
			}
			else if (horizontalScrollingLeftCol && pressed.y == 7) {
				handlePad(modelStackWithTimelineCounter, leftColPrev, pressed);
				horizontalScrollingLeftCol = false;
				continue;
			}
			if (!horizontalScrollingLeftCol && !pressed.dead) {
				handlePad(modelStackWithTimelineCounter, leftCol, pressed);
			}
		}
		else if (pressed.x == RIGHT_COL) {
			if (pressed.active) {
				rightColHeld = pressed.y;
			}
			else if (horizontalScrollingRightCol && pressed.y == 7) {
				handlePad(modelStackWithTimelineCounter, rightColPrev, pressed);
				horizontalScrollingRightCol = false;
				continue;
			}
			if (!horizontalScrollingRightCol && !pressed.dead) {
				handlePad(modelStackWithTimelineCounter, rightCol, pressed);
			}
		}
	}
}

void ColumnControlsKeyboard::handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                       ColumnControlFunction func, PressedPad pad) {
	switch (func) {
	case VELOCITY:
		if (pad.active) {
			vDisplay = velocityMin + pad.y * velocityStep;
			velocity = (vDisplay + kHalfStep) >> kVelModShift;
			display->displayPopup(velocity);
		}
		else if (!pad.padPressHeld || FlashStorage::keyboardFunctionsVelocityGlide) {
			velocity32 = velocityMin + pad.y * velocityStep;
			vDisplay = velocity32;
			velocity = (velocity32 + kHalfStep) >> kVelModShift;
		}
		else {
			vDisplay = velocity32;
			velocity = (velocity32 + kHalfStep) >> kVelModShift;
		}
		break;
	case MOD:
		if (pad.active) {
			modDisplay = modMin + pad.y * modStep;
			getCurrentInstrument()->processParamFromInputMIDIChannel(CC_NUMBER_Y_AXIS, modDisplay,
			                                                         modelStackWithTimelineCounter);
			display->displayPopup((modDisplay + kHalfStep) >> kVelModShift);
		}
		else if (!pad.padPressHeld || FlashStorage::keyboardFunctionsModwheelGlide) {
			mod32 = modMin + pad.y * modStep;
			getCurrentInstrument()->processParamFromInputMIDIChannel(CC_NUMBER_Y_AXIS, mod32,
			                                                         modelStackWithTimelineCounter);
		}
		else {
			modDisplay = mod32;
			getCurrentInstrument()->processParamFromInputMIDIChannel(CC_NUMBER_Y_AXIS, mod32,
			                                                         modelStackWithTimelineCounter);
		}
		break;
	case CHORD:
		if (pad.active) {
			setActiveChord(static_cast<ChordModeChord>(pad.y + 1));
			display->displayPopup(chordNames[activeChord]);
		}
		else if (!pad.padPressHeld) {
			if (defaultChord != static_cast<ChordModeChord>(pad.y + 1)) {
				defaultChord = static_cast<ChordModeChord>(pad.y + 1);
			}
			else {
				defaultChord = NO_CHORD;
			}
			setActiveChord(defaultChord);
			display->displayPopup(chordNames[activeChord]);
		}
		else {
			setActiveChord(defaultChord);
		}
		break;
	case CHORD_MEM:
		if (pad.active) {
			activeChordMem = pad.y;
			auto noteCount = chordMemNoteCount[pad.y];
			for (int i = 0; i < noteCount && i < kMaxNotesChordMem; i++) {
				currentNotesState.enableNote(chordMem[pad.y][i], velocity);
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
		break;
	case SCALE_MODE:
		if (pad.active) {
			if (keyboardScreen.setScale(scaleModes[pad.y])) {
				currentScalePad = pad.y;
			}
		}
		else if (!pad.padPressHeld) {
			// Pad released after short press
			if (keyboardScreen.setScale(scaleModes[pad.y])) {
				previousScalePad = pad.y;
				currentScalePad = pad.y;
			}
		}
		else {
			// Pad released after long press
			if (keyboardScreen.setScale(scaleModes[previousScalePad])) {
				currentScalePad = previousScalePad;
			}
		}
		break;
		// case BEAT_REPEAT:
		//	/* TODO */
		//	break;
	}
}

void ColumnControlsKeyboard::setActiveChord(ChordModeChord chord) {
	activeChord = chord;
	chordSemitoneOffsets[0] = chordTypeSemitoneOffsets[activeChord][0];
	chordSemitoneOffsets[1] = chordTypeSemitoneOffsets[activeChord][1];
	chordSemitoneOffsets[2] = chordTypeSemitoneOffsets[activeChord][2];
	chordSemitoneOffsets[3] = chordTypeSemitoneOffsets[activeChord][3];
}

void ColumnControlsKeyboard::handleVerticalEncoder(int32_t offset) {
	verticalEncoderHandledByColumns(offset);
}

bool ColumnControlsKeyboard::verticalEncoderHandledByColumns(int32_t offset) {
	if (leftColHeld != -1) {
		return verticalEncoderHandledByFunc(leftCol, leftColHeld, offset);
	}
	if (rightColHeld != -1) {
		return verticalEncoderHandledByFunc(rightCol, rightColHeld, offset);
	}
	return false;
}

bool ColumnControlsKeyboard::verticalEncoderHandledByFunc(ColumnControlFunction func, int8_t pad, int32_t offset) {
	switch (func) {
	case VELOCITY:
		if (pad == 7) {
			velocityMax += offset << kVelModShift;
			velocityMax = std::clamp(velocityMax, velocityMin, (uint32_t)127 << kVelModShift);
			display->displayPopup(velocityMax >> kVelModShift);
			velocityStep = (velocityMax - velocityMin) / 7;
			return true;
		}
		else if (pad == 0) {
			velocityMin += offset << kVelModShift;
			velocityMin = std::clamp((int32_t)velocityMin, (int32_t)0, (int32_t)velocityMax);
			display->displayPopup(velocityMin >> kVelModShift);
			velocityStep = (velocityMax - velocityMin) / 7;
			return true;
		}
	case SCALE_MODE: {
		int32_t newScale = ((int32_t)scaleModes[pad] + offset) % NUM_PRESET_SCALES;
		if (newScale < 0) {
			newScale = NUM_PRESET_SCALES - 1;
		}
		scaleModes[pad] = newScale;
		return true;
	}
	case MOD:
		if (pad == 7) {
			modMax += offset << kVelModShift;
			modMax = std::clamp(modMax, modMin, (uint32_t)127 << kVelModShift);
			display->displayPopup(modMax >> kVelModShift);
			modStep = (modMax - modMin) / 7;
			return true;
		}
		else if (pad == 0) {
			modMin += offset << kVelModShift;
			modMin = std::clamp((int32_t)modMin, (int32_t)0, (int32_t)modMax);
			display->displayPopup(modMin >> kVelModShift);
			modStep = (modMax - modMin) / 7;
			return true;
		}
	}
	return false;
}

void ColumnControlsKeyboard::handleHorizontalEncoder(int32_t offset, bool shiftEnabled) {
	horizontalEncoderHandledByColumns(offset, shiftEnabled);
}

ColumnControlFunction nextControlFunction(ColumnControlFunction cur, ColumnControlFunction skip) {
	auto out = static_cast<ColumnControlFunction>((cur + 1) % COL_CTRL_FUNC_MAX);
	if (out == skip) {
		out = static_cast<ColumnControlFunction>((out + 1) % COL_CTRL_FUNC_MAX);
	}
	return out;
}

ColumnControlFunction prevControlFunction(ColumnControlFunction cur, ColumnControlFunction skip) {
	auto out = static_cast<ColumnControlFunction>(cur - 1);
	if (out < 0) {
		out = static_cast<ColumnControlFunction>(COL_CTRL_FUNC_MAX - 1);
	}
	if (out == skip) {
		out = static_cast<ColumnControlFunction>(out - 1);
		if (out < 0) {
			out = static_cast<ColumnControlFunction>(COL_CTRL_FUNC_MAX - 1);
		}
	}
	return out;
}

bool ColumnControlsKeyboard::horizontalEncoderHandledByColumns(int32_t offset, bool shiftEnabled) {
	if (leftColHeld == 7) {
		if (!horizontalScrollingLeftCol) {
			leftColPrev = leftCol;
		}
		horizontalScrollingLeftCol = true;
		leftCol = (offset > 0) ? nextControlFunction(leftCol, rightCol) : prevControlFunction(leftCol, rightCol);
		display->displayPopup(functionNames[leftCol]);
		return true;
	}
	if (rightColHeld == 7) {
		if (!horizontalScrollingRightCol) {
			rightColPrev = rightCol;
		}
		horizontalScrollingRightCol = true;
		rightCol = (offset > 0) ? nextControlFunction(rightCol, leftCol) : prevControlFunction(rightCol, leftCol);
		display->displayPopup(functionNames[rightCol]);
		return true;
	}
	return false;
}

void ColumnControlsKeyboard::renderSidebarPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	// Iterate over velocity and mod pads in sidebar
	switch (leftCol) {
	case VELOCITY:
		renderColumnVelocity(image, LEFT_COL);
		break;
	case MOD:
		renderColumnMod(image, LEFT_COL);
		break;
	case CHORD:
		renderColumnChord(image, LEFT_COL);
		break;
	case CHORD_MEM:
		renderColumnChordMem(image, LEFT_COL);
		break;
	case SCALE_MODE:
		renderColumnScaleMode(image, LEFT_COL);
		break;
		// case BEAT_REPEAT:
		//	renderColumnBeatRepeat(image, LEFT_COL);
		//	break;
	}

	switch (image, rightCol) {
	case VELOCITY:
		renderColumnVelocity(image, RIGHT_COL);
		break;
	case MOD:
		renderColumnMod(image, RIGHT_COL);
		break;
	case CHORD:
		renderColumnChord(image, RIGHT_COL);
		break;
	case CHORD_MEM:
		renderColumnChordMem(image, RIGHT_COL);
		break;
	case SCALE_MODE:
		renderColumnScaleMode(image, RIGHT_COL);
		break;
		// case BEAT_REPEAT:
		//	renderColumnBeatRepeat(image, RIGHT_COL);
		//	break;
	}
}

void ColumnControlsKeyboard::renderColumnVelocity(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column) {
	uint8_t brightness = 1;
	uint8_t otherChannels = 0;
	uint32_t velocityVal = velocityMin;

	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		bool velocity_selected =
		    vDisplay >= (y > 0 ? (velocityVal - (velocityStep - 1)) : 0) && vDisplay <= velocityVal + kHalfStep;
		otherChannels = velocity_selected ? 0xf0 : 0;
		uint8_t base = velocity_selected ? 0xff : brightness + 0x04;
		image[y][column] = {base, otherChannels, otherChannels};
		velocityVal += velocityStep;
		brightness += 10;
	}
}

void ColumnControlsKeyboard::renderColumnMod(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column) {
	uint8_t brightness = 1;
	uint8_t otherChannels = 0;
	uint32_t modVal = modMin;

	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		bool mod_selected = modDisplay >= (y > 0 ? (modVal - (modStep - 1)) : 0) && modDisplay <= modVal;
		otherChannels = mod_selected ? 0xf0 : 0;
		uint8_t base = mod_selected ? 0xff : brightness + 0x04;
		image[y][column] = {otherChannels, otherChannels, base};
		modVal += modStep;
		brightness += 10;
	}
}

void ColumnControlsKeyboard::renderColumnChord(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column) {
	uint8_t otherChannels = 0;
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		bool chord_selected = y + 1 == activeChord;
		otherChannels = chord_selected ? 0xf0 : 0;
		uint8_t base = chord_selected ? 0xff : 0x7F;
		image[y][column] = {otherChannels, base, otherChannels};
	}
}

void ColumnControlsKeyboard::renderColumnBeatRepeat(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column) {
	// TODO
	uint8_t otherChannels = 0;
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		bool beat_selected = false;
		otherChannels = beat_selected ? 0xf0 : 0;
		uint8_t base = beat_selected ? 0xff : 0x50;
		image[y][column] = {base, otherChannels, base};
	}
}

void ColumnControlsKeyboard::renderColumnChordMem(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column) {
	uint8_t otherChannels = 0;
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		bool chord_selected = y == activeChordMem;
		uint8_t chord_slot_filled = chordMemNoteCount[y] > 0 ? 0x7f : 0;
		otherChannels = chord_selected ? 0xf0 : 0;
		uint8_t base = chord_selected ? 0xff : chord_slot_filled;
		image[y][column] = {otherChannels, base, base};
	}
}

void ColumnControlsKeyboard::renderColumnScaleMode(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column) {
	uint8_t otherChannels = 0;
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		bool mode_selected = y == currentScalePad;
		uint8_t mode_available = y < NUM_PRESET_SCALES ? 0x7f : 0;
		otherChannels = mode_selected ? 0xf0 : 0;
		uint8_t base = mode_selected ? 0xff : mode_available;
		image[y][column] = {base, base, otherChannels};
	}
}

} // namespace deluge::gui::ui::keyboard::layout
