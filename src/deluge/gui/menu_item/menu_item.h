/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "RZA1/system/r_typedefs.h"
#include "definitions.h"

#define MENU_PERMISSION_NO 0
#define MENU_PERMISSION_YES 1
#define MENU_PERMISSION_MUST_SELECT_RANGE 2

#define MENU_ITEM_TITLE_BUFFER_SIZE 20 // Actual max num chars for OLED display is 14.

class Sound;
class MultiRange;
class MIDIDevice;

class MenuItem {
public:
	MenuItem(char const* newName = nullptr, char const* newTitle = nullptr) : name(newName) {
		if (newTitle == nullptr) {
			title = newTitle;
		}
	}

	char const* name; // As viewed in a menu list. For OLED, up to 20 chars.
	virtual char const* getName() { return name; }

	virtual void horizontalEncoderAction(int offset) {}
	virtual void selectEncoderAction(int offset) {}
	virtual void beginSession(MenuItem* navigatedBackwardFrom = NULL){};
	virtual bool isRelevant(Sound* sound, int whichThing) { return true; }
	virtual MenuItem* selectButtonPress() { return nullptr; }
	virtual int checkPermissionToBeginSession(Sound* sound, int whichThing, MultiRange** currentRange);
	virtual void readValueAgain() {}
	virtual bool selectEncoderActionEditsInstrument() { return false; }
	virtual uint8_t getPatchedParamIndex() { return 255; }
	virtual uint8_t getIndexOfPatchedParamToBlink() { return 255; }
	virtual uint8_t shouldDrawDotOnName() { return 255; }
	virtual uint8_t shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour) { return 255; }
	virtual MenuItem* patchingSourceShortcutPress(int s, bool previousPressStillActive = false) {
		return NULL;
	} // NULL means do nothing. 0xFFFFFFFF means go up a level
	virtual void unlearnAction() {}
	virtual bool allowsLearnMode() { return false; }
	virtual void learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) {}
	virtual bool learnNoteOn(MIDIDevice* fromDevice, int channel, int noteCode) {
		return false;
	} // Returns whether it was used, I think?
	virtual void learnCC(MIDIDevice* fromDevice, int channel, int ccNumber, int value);
	virtual bool shouldBlinkLearnLed() { return false; }
	virtual bool isRangeDependent() { return false; }
	virtual bool usesAffectEntire() { return false; }

	char const* title; // Can get overridden by getTitle(). Actual max num chars for OLED display is 14.
#if HAVE_OLED
	virtual void renderOLED();
	virtual void drawPixelsForOled() {
	}
	void drawItemsForOled(char const** options, int selectedOption);

	/// Get the title to be used when rendering on OLED. If not overriden, defaults to returning `title`.
	virtual char const* getTitle();

#else
	virtual void drawName();

#endif
};
