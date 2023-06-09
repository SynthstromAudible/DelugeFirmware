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

#ifndef KEYBOARDSCREEN_H_
#define KEYBOARDSCREEN_H_

#include <RootUI.h>
#include "UI.h"
#include "InstrumentClipMinder.h"

struct KeyboardPadPress {
	uint8_t x;
	uint8_t y;
};

#define MAX_NUM_KEYBOARD_PAD_PRESSES 10

class ModelStack;

class KeyboardScreen final : public RootUI, public InstrumentClipMinder {
public:
	KeyboardScreen();
	bool opened();
	void focusRegained();
	int padAction(int x, int y, int velocity);
	int buttonAction(int x, int y, bool on, bool inCardRoutine);
	bool renderMainPads(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
	                    uint8_t occupancyMask[][displayWidth + sideBarWidth], bool drawUndefinedArea = false);
	bool renderSidebar(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
	                   uint8_t occupancyMask[][displayWidth + sideBarWidth]);
	int verticalEncoderAction(int offset, bool inCardRoutine);
	int horizontalEncoderAction(int offset);
	void selectEncoderAction(int8_t offset);
	ClipMinder* toClipMinder() { return this; }
	void flashDefaultRootNote();
	bool oneNoteAuditioning();
	void setLedStates();
	void recalculateColours();
	bool getAffectEntire();
	void graphicsRoutine();
	void exitAuditionMode();
	void openedInBackground();
	void stopAllAuditioning(ModelStack* modelStack, bool switchOffOnThisEndToo = true);

#if HAVE_OLED
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
		InstrumentClipMinder::renderOLED(image);
	}
#endif

private:
	int getNoteCodeFromCoords(int x, int y);
	void doScroll(int offset);
	int getLowestAuditionedNote();
	int getHighestAuditionedNote();
	void enterScaleMode(int selectedRootNote = 2147483647);
	void exitScaleMode();
	void drawNoteCode(int noteCode);

	KeyboardPadPress padPresses[MAX_NUM_KEYBOARD_PAD_PRESSES];
	uint8_t noteColours[displayHeight * KEYBOARD_ROW_INTERVAL + displayWidth][3];
	bool yDisplayActive[displayHeight * KEYBOARD_ROW_INTERVAL + displayWidth];
};

extern KeyboardScreen keyboardScreen;
#endif /* KEYBOARDSCREEN_H_ */
