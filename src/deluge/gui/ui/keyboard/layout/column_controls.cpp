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
#include "processing/sound/sound_instrument.h"
#include "storage/flash_storage.h"
#include "util/functions.h"
#include "util/lookuptables/lookuptables.h"
#include <limits>

#define LEFT_COL kDisplayWidth
#define RIGHT_COL (kDisplayWidth + 1)

using namespace deluge::gui::ui::keyboard::controls;

namespace deluge::gui::ui::keyboard::layout {

PLACE_SDRAM_DATA l10n::String functionNames[] = {
    l10n::String::STRING_FOR_COLUMN_VELOCITY, //<
    l10n::String::STRING_FOR_COLUMN_MOD,
    l10n::String::STRING_FOR_COLUMN_CHORD,
    l10n::String::STRING_FOR_COLUMN_SONG_CHORD_MEM,
    l10n::String::STRING_FOR_COLUMN_CHORD_MEM,
    l10n::String::STRING_FOR_COLUMN_SCALE_MODE,
    l10n::String::STRING_FOR_COLUMN_DX,
    l10n::String::STRING_FOR_COLUMN_SESSION,
    l10n::String::STRING_FOR_COLUMN_BEAT_REPEAT,
};

PLACE_SDRAM_TEXT void ColumnControlsKeyboard::enableNote(uint8_t note, uint8_t velocity) {
	currentNotesState.enableNote(note, velocity);

	ColumnControlState& state = getState().columnControl;

	for (int i = 1; i < MAX_CHORD_NOTES; i++) {
		auto offset = state.chordColumn.chordSemitoneOffsets[i];
		if (!offset) {
			break;
		}
		currentNotesState.enableNote(note + offset, velocity, true);
	}
}

PLACE_SDRAM_TEXT void ColumnControlsKeyboard::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(getCurrentClip());

	leftColHeld = -1;

	rightColHeld = -1;

	ColumnControlState& state = getState().columnControl;

	for (int32_t idxPress = 0; idxPress < kMaxNumKeyboardPadPresses; ++idxPress) {
		auto pressed = presses[idxPress];
		// in note columns
		if (pressed.x == LEFT_COL) {
			if (pressed.active) {
				// if holding multiple will be left as the last button pressed
				leftColHeld = pressed.y;
				// needs to be wrapped in a community features toggle since there's no UI for this
				if (pressed.y == 7) {
					if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::EnableKeyboardViewSidebarMenuExit)
					    == RuntimeFeatureStateToggle::On) {
						// minimize happy path time since most of the time this will be a normal press
						if (getCurrentUI()->exitUI()) [[unlikely]] {
							keyboardScreen.killColumnSwitchKey(LEFT_COL);
							continue;
						}
					}
				}
			}

			if (!pressed.dead) {
				state.leftCol->handlePad(modelStackWithTimelineCounter, pressed, this);
			}
		}
		else if (pressed.x == RIGHT_COL) {
			if (pressed.active) {
				rightColHeld = pressed.y;
			}
			if (!pressed.dead) {
				state.rightCol->handlePad(modelStackWithTimelineCounter, pressed, this);
			}
		}
	}
}

PLACE_SDRAM_TEXT void ColumnControlsKeyboard::handleVerticalEncoder(int32_t offset) {
	verticalEncoderHandledByColumns(offset);
}

PLACE_SDRAM_TEXT bool ColumnControlsKeyboard::verticalEncoderHandledByColumns(int32_t offset) {
	ColumnControlState& state = getState().columnControl;

	if (leftColHeld != -1) {
		return state.leftCol->handleVerticalEncoder(leftColHeld, offset);
	}
	if (rightColHeld != -1) {
		return state.rightCol->handleVerticalEncoder(rightColHeld, offset);
	}
	return false;
}

PLACE_SDRAM_TEXT void ColumnControlsKeyboard::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                                      PressedPad presses[kMaxNumKeyboardPadPresses],
                                                                      bool encoderPressed) {
	horizontalEncoderHandledByColumns(offset, shiftEnabled);
}

PLACE_SDRAM_TEXT ColumnControlFunction ColumnControlsKeyboard::nextControlFunction(ColumnControlFunction cur,
                                                                                   ColumnControlFunction skip) {
	bool has_dx = (getCurrentDxPatch() != nullptr);
	auto out = cur;
	while (true) {
		out = static_cast<ColumnControlFunction>((out + 1) % COL_CTRL_FUNC_MAX);
		if (out != skip && (has_dx || out != DX) && allowSidebarType(out)) {
			return out;
		}
	}
}

PLACE_SDRAM_TEXT ColumnControlFunction ColumnControlsKeyboard::prevControlFunction(ColumnControlFunction cur,
                                                                                   ColumnControlFunction skip) {
	bool has_dx = (getCurrentDxPatch() != nullptr);
	auto out = cur;
	while (true) {
		out = static_cast<ColumnControlFunction>(out - 1);
		if (out < 0) {
			out = static_cast<ColumnControlFunction>(COL_CTRL_FUNC_MAX - 1);
		}
		if (out != skip && (has_dx || out != DX) && allowSidebarType(out)) {
			return out;
		}
	}
}

PLACE_SDRAM_TEXT void ColumnControlsKeyboard::checkNewInstrument(Instrument* newInstrument) {
	if (newInstrument->type != OutputType::SYNTH) {
		return;
	}
	ColumnControlState& state = getState().columnControl;
	auto* sound = (SoundInstrument*)newInstrument;
	bool is_dx = (sound->sources[0].oscType == OscType::DX7);
	if (!is_dx) {
		if (state.rightColFunc == DX) {
			state.rightColSetAtRuntime = false;
			state.rightColFunc = nextControlFunction(state.rightColFunc, state.leftColFunc);
			state.rightCol = state.getColumnForFunc(state.rightColFunc);
		}
		else if (state.leftColFunc == DX) {
			state.leftColFunc = nextControlFunction(state.leftColFunc, state.rightColFunc);
			state.leftCol = state.getColumnForFunc(state.leftColFunc);
		}
	}
	else {
		if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::EnableDX7Engine) == RuntimeFeatureStateToggle::Off) {
			return;
		}
		// don't change column if already set
		if (state.leftColFunc == DX || state.rightColFunc == DX || state.rightColSetAtRuntime) {
			return;
		}
		state.rightColFunc = DX;
		state.rightCol = state.getColumnForFunc(state.rightColFunc);
	}
}

PLACE_SDRAM_TEXT ColumnControlFunction ColumnControlsKeyboard::stepControlFunction(int32_t offset,
                                                                                   ColumnControlFunction cur,
                                                                                   ColumnControlFunction skip) {
	return (offset > 0) ? nextControlFunction(cur, skip) : prevControlFunction(cur, skip);
}

PLACE_SDRAM_TEXT ControlColumn* ColumnControlState::getColumnForFunc(ColumnControlFunction func) {
	switch (func) {
	case VELOCITY:
		return &velocityColumn;
	case MOD:
		return &modColumn;
	case CHORD:
		return &chordColumn;
	case SONG_CHORD_MEM:
		return &songChordMemColumn;
	case CHORD_MEM:
		return &chordMemColumn;
	case SCALE_MODE:
		return &scaleModeColumn;
	case DX:
		return &dxColumn;
	case SESSION:
		return &sessionColumn;
	// explicit fallthrough cases
	case COL_CTRL_FUNC_MAX:;
	}
	return nullptr;
}

const char* columnFunctionToString(ColumnControlFunction func) {
	switch (func) {
	case VELOCITY:
		return "velocity";
	case MOD:
		return "mod";
	case CHORD:
		return "chord";
	case SONG_CHORD_MEM:
		return "song_chord_mem";
	case CHORD_MEM:
		return "clip_chord_mem";
	case SCALE_MODE:
		return "scale_mode";
	case DX:
		return "dx";
	case SESSION:
		return "session";
	// explicit fallthrough cases
	case COL_CTRL_FUNC_MAX: // counter: should not appear here
	    ;
	}
	return "";
}

PLACE_SDRAM_TEXT ColumnControlFunction stringToColumnFunction(char const* string) {
	if (!strcmp(string, "velocity")) {
		return VELOCITY;
	}
	else if (!strcmp(string, "mod")) {
		return MOD;
	}
	else if (!strcmp(string, "chord")) {
		return CHORD;
	}
	else if (!strcmp(string, "song_chord_mem")) {
		return SONG_CHORD_MEM;
	}
	else if (!strcmp(string, "clip_chord_mem")) {
		return CHORD_MEM;
	}
	else if (!strcmp(string, "scale_mode")) {
		return SCALE_MODE;
	}
	else if (!strcmp(string, "dx")) {
		return DX;
	}
	else if (!strcmp(string, "session")) {
		return SESSION;
	}
	else {
		return VELOCITY; // unknown column, just pick the default
	}
}

PLACE_SDRAM_TEXT void ColumnControlState::writeToFile(Serializer& writer) {
	writer.writeOpeningTagBeginning("leftCol");
	writer.writeAttribute("type", (char*)columnFunctionToString(leftColFunc));
	writer.closeTag();

	writer.writeOpeningTagBeginning("rightCol");
	writer.writeAttribute("type", (char*)columnFunctionToString(rightColFunc));
	writer.closeTag();

	chordMemColumn.writeToFile(writer);
}

PLACE_SDRAM_TEXT void ColumnControlState::readFromFile(Deserializer& reader) {
	char const* tagName;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		bool isLeft = !strcmp(tagName, "leftCol");
		if (isLeft || !strcmp(tagName, "rightCol")) {
			reader.match('{');
			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "type")) {
					auto value = stringToColumnFunction(reader.readTagOrAttributeValue());
					(isLeft ? leftColFunc : rightColFunc) = value;
					reader.exitTag("type");
				}
				else {
					reader.exitTag(tagName);
				}
			}
			reader.match('}');
		}
		else if (!strcmp(tagName, "chordMem")) {
			chordMemColumn.readFromFile(reader);
		}
	}
	reader.match('}');
	leftCol = getColumnForFunc(leftColFunc);
	rightCol = getColumnForFunc(rightColFunc);
}

PLACE_SDRAM_TEXT bool ColumnControlsKeyboard::horizontalEncoderHandledByColumns(int32_t offset, bool shiftEnabled) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(getCurrentClip());

	ColumnControlState& state = getState().columnControl;
	if (leftColHeld == 7 && offset) {
		state.leftCol->handleLeavingColumn(modelStackWithTimelineCounter, this);
		state.leftColFunc = (offset > 0) ? nextControlFunction(state.leftColFunc, state.rightColFunc)
		                                 : prevControlFunction(state.leftColFunc, state.rightColFunc);
		display->displayPopup(l10n::get(functionNames[state.leftColFunc]));
		state.leftCol = state.getColumnForFunc(state.leftColFunc);
		keyboardScreen.killColumnSwitchKey(LEFT_COL);
		return true;
	}
	else if (rightColHeld == 7 && offset) {
		state.rightCol->handleLeavingColumn(modelStackWithTimelineCounter, this);
		state.rightColFunc = (offset > 0) ? nextControlFunction(state.rightColFunc, state.leftColFunc)
		                                  : prevControlFunction(state.rightColFunc, state.leftColFunc);
		display->displayPopup(l10n::get(functionNames[state.rightColFunc]));
		state.rightCol = state.getColumnForFunc(state.rightColFunc);
		state.rightColSetAtRuntime = true;
		keyboardScreen.killColumnSwitchKey(RIGHT_COL);
		return true;
	}
	return false;
}

PLACE_SDRAM_TEXT void ColumnControlsKeyboard::renderSidebarPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	ColumnControlState& state = getState().columnControl;

	state.leftCol->renderColumn(image, LEFT_COL, this);
	state.rightCol->renderColumn(image, RIGHT_COL, this);
}

PLACE_SDRAM_TEXT void ColumnControlsKeyboard::renderColumnBeatRepeat(RGB image[][kDisplayWidth + kSideBarWidth],
                                                                     int32_t column) {
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
