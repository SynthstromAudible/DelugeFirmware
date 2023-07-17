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

class ModelStack;

namespace keyboard {



#define MAX_NUM_KEYBOARD_PAD_PRESSES 10

class KeyboardScreen final : public RootUI, public InstrumentClipMinder {
public:
	KeyboardScreen();

	int padAction(int x, int y, int velocity);
	int buttonAction(hid::Button b, bool on, bool inCardRoutine);
	int verticalEncoderAction(int offset, bool inCardRoutine);
	int horizontalEncoderAction(int offset);
	void selectEncoderAction(int8_t offset);


	bool renderMainPads(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
	                    uint8_t occupancyMask[][displayWidth + sideBarWidth], bool drawUndefinedArea = false);
	bool renderSidebar(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
	                   uint8_t occupancyMask[][displayWidth + sideBarWidth]);

	void flashDefaultRootNote();
	void recalculateColours();
	void openedInBackground();
	void exitAuditionMode();

private:
	bool opened();
	void focusRegained();

	ClipMinder* toClipMinder() { return this; }
	bool oneNoteAuditioning();
	void setLedStates();
	void graphicsRoutine();
	void stopAllAuditioning(ModelStack* modelStack, bool switchOffOnThisEndToo = true);
	bool getAffectEntire();


#if HAVE_OLED
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
		InstrumentClipMinder::renderOLED(image);
	}
#endif

private:
	void doScroll(int offset, bool force = false);
	int getLowestAuditionedNote();
	int getHighestAuditionedNote();
	void enterScaleMode(int selectedRootNote = 2147483647);
	void exitScaleMode();
	void drawNoteCode(int noteCode);

public:
	uint8_t noteColours[displayHeight * KEYBOARD_ROW_INTERVAL_MAX + displayWidth][3];

};

}; // namespace keyboard

extern keyboard::KeyboardScreen keyboardScreen;
