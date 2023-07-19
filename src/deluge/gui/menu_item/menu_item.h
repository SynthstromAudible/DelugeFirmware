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
#include "util/container/static_vector.hpp"
#include "util/sized.h"
#include "util/string.h"

#if HAVE_OLED
#include "hid/display/oled.h"
#endif

#define MENU_PERMISSION_NO 0
#define MENU_PERMISSION_YES 1
#define MENU_PERMISSION_MUST_SELECT_RANGE 2

#define MENU_ITEM_TITLE_BUFFER_SIZE 20 // Actual max num chars for OLED display is 14.

class Sound;
class MultiRange;
class MIDIDevice;

class MenuItem {
public:
	MenuItem(const deluge::string& newName = "", const deluge::string& newTitle = "") : name(newName), title(newTitle) {
		if (newTitle.empty()) {
			title = newName;
		}
	}

	deluge::string name; // As viewed in a menu list. For OLED, up to 20 chars.
	[[nodiscard]] virtual const deluge::string& getName() const { return name; }

	virtual void horizontalEncoderAction(int offset) {}
	virtual void selectEncoderAction(int offset) {}
	virtual void beginSession(MenuItem* navigatedBackwardFrom = nullptr){};
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
		return nullptr; // nullptr means do nothing. 0xFFFFFFFF means go up a level
	}

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

	deluge::string title; // Can get overridden by getTitle(). Actual max num chars for OLED display is 14.

	/// Get the title to be used when rendering on OLED. If not overriden, defaults to returning `title`.
	[[nodiscard]] virtual const deluge::string &getTitle() const { return title; }

#if HAVE_OLED
	virtual void renderOLED();
	virtual void drawPixelsForOled() {}

	template <size_t n>
	static void drawItemsForOled(deluge::static_vector<deluge::string, n>& options, int selectedOption, int offset = 0);
#else
	/// Get the title to be used when rendering on OLED. If not overriden, defaults to returning `title`.
	virtual void drawName();
#endif
};

#if HAVE_OLED
// A couple of our child classes call this - that's all
template <size_t n>
void MenuItem::drawItemsForOled(deluge::static_vector<deluge::string, n>& options, const int selectedOption,
                                const int offset) {
	int baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;

	auto* it = std::next(options.begin(), offset); // fast-forward to the first option visible
	for (int o = 0; o < OLED_HEIGHT_CHARS - 1 && o < options.size() - offset; o++) {
		int yPixel = o * TEXT_SPACING_Y + baseY;

		OLED::drawString(options[o + offset].c_str(), TEXT_SPACING_X, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
		                 TEXT_SPACING_X, TEXT_SPACING_Y);

		if (o == selectedOption) {
			OLED::invertArea(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8, &OLED::oledMainImage[0]);
			OLED::setupSideScroller(0, options[o + offset].c_str(), TEXT_SPACING_X, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8,
			                        TEXT_SPACING_X, TEXT_SPACING_Y, true);
		}
	}
}
#endif
