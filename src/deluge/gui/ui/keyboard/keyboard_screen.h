/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#pragma once

#include "gui/ui/root_ui.h"
#include "gui/ui/ui.h"
#include "hid/button.h"
#include "model/clip/instrument_clip_minder.h"
#include "gui/ui/keyboard/layout.h"
#include <limits>

class ModelStack;

namespace keyboard {

#define MAX_NUM_KEYBOARD_PAD_PRESSES 10

class KeyboardScreen final : public RootUI, public InstrumentClipMinder {
public:
	KeyboardScreen();

	ActionResult padAction(int x, int y, int velocity);
	ActionResult buttonAction(hid::Button b, bool on, bool inCardRoutine);
	ActionResult verticalEncoderAction(int offset, bool inCardRoutine);
	ActionResult horizontalEncoderAction(int offset);
	void selectEncoderAction(int8_t offset);

	bool renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = false);
	bool renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);

	void flashDefaultRootNote();
	void openedInBackground();
	void exitAuditionMode();

private:
	bool opened();
	void focusRegained();

	void evaluateActiveNotes();
	void updateActiveNotes();

	ClipMinder* toClipMinder() { return this; }
	void setLedStates();
	void graphicsRoutine();
	bool getAffectEntire();

#if HAVE_OLED
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
		InstrumentClipMinder::renderOLED(image);
	}
#endif

private:
	void enterScaleMode(int selectedRootNote = std::numeric_limits<int>::max());
	void exitScaleMode();
	void drawNoteCode(int noteCode);

	inline void requestRendering() {
		uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
	}

	PressedPad pressedPads[MAX_NUM_KEYBOARD_PAD_PRESSES];
	NotesState lastNoteState;
	NotesState currentNoteState;
};

}; // namespace keyboard

extern keyboard::KeyboardScreen keyboardScreen;
