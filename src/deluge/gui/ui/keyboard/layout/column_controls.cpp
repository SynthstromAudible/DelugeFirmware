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

using namespace deluge::gui::ui::keyboard::controls;

namespace deluge::gui::ui::keyboard::layout {

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
		auto offset = chordColumn.chordSemitoneOffsets[i];
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
				leftColPrev->handlePad(modelStackWithTimelineCounter, pressed, this);
				horizontalScrollingLeftCol = false;
				continue;
			}
			if (!horizontalScrollingLeftCol && !pressed.dead) {
				leftCol->handlePad(modelStackWithTimelineCounter, pressed, this);
			}
		}
		else if (pressed.x == RIGHT_COL) {
			if (pressed.active) {
				rightColHeld = pressed.y;
			}
			else if (horizontalScrollingRightCol && pressed.y == 7) {
				rightColPrev->handlePad(modelStackWithTimelineCounter, pressed, this);
				horizontalScrollingRightCol = false;
				continue;
			}
			if (!horizontalScrollingRightCol && !pressed.dead) {
				rightCol->handlePad(modelStackWithTimelineCounter, pressed, this);
			}
		}
	}
}

void ColumnControlsKeyboard::handleVerticalEncoder(int32_t offset) {
	verticalEncoderHandledByColumns(offset);
}

bool ColumnControlsKeyboard::verticalEncoderHandledByColumns(int32_t offset) {
	if (leftColHeld != -1) {
		return leftCol->handleVerticalEncoder(leftColHeld, offset);
	}
	if (rightColHeld != -1) {
		return rightCol->handleVerticalEncoder(rightColHeld, offset);
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

ControlColumn* ColumnControlsKeyboard::getColumnForFunc(ColumnControlFunction func) {
	switch (func) {
	case VELOCITY:
		return &velocityColumn;
	case MOD:
		return &modColumn;
	case CHORD:
		return &chordColumn;
	case CHORD_MEM:
		return &chordMemColumn;
	case SCALE_MODE:
		return &scaleModeColumn;
	}
	return nullptr;
}

bool ColumnControlsKeyboard::horizontalEncoderHandledByColumns(int32_t offset, bool shiftEnabled) {
	if (leftColHeld == 7 && offset) {
		if (!horizontalScrollingLeftCol) {
			leftColPrev = leftCol;
		}
		horizontalScrollingLeftCol = true;
		leftColFunc = (offset > 0) ? nextControlFunction(leftColFunc, rightColFunc)
		                           : prevControlFunction(leftColFunc, rightColFunc);
		display->displayPopup(functionNames[leftColFunc]);
		leftCol = getColumnForFunc(leftColFunc);
		return true;
	}
	else if (rightColHeld == 7 && offset) {
		if (!horizontalScrollingRightCol) {
			rightColPrev = rightCol;
		}
		horizontalScrollingRightCol = true;
		rightColFunc = (offset > 0) ? nextControlFunction(rightColFunc, leftColFunc)
		                            : prevControlFunction(rightColFunc, leftColFunc);
		display->displayPopup(functionNames[rightColFunc]);
		rightCol = getColumnForFunc(rightColFunc);
		return true;
	}
	return false;
}

void ColumnControlsKeyboard::renderSidebarPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	leftCol->renderColumn(image, LEFT_COL);
	rightCol->renderColumn(image, RIGHT_COL);
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

} // namespace deluge::gui::ui::keyboard::layout
