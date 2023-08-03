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

#include "definitions_cxx.hpp"
#include "hid/display.h"
#include "util/container/static_vector.hpp"
#include "util/sized.h"
#include "util/string.h"
#include <cstdint>

enum class MenuPermission {
	NO,
	YES,
	MUST_SELECT_RANGE,
};

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

	MenuItem(const MenuItem& other) = delete;
	MenuItem(const MenuItem&& other) = delete;
	MenuItem& operator=(const MenuItem& other) = delete;
	MenuItem& operator=(const MenuItem&& other) = delete;

	virtual ~MenuItem() = default;

	/// As viewed in a menu list. For OLED, up to 20 chars.
	deluge::string name;
	[[nodiscard]] virtual std::string_view getName() const { return name; }

	virtual void horizontalEncoderAction(int32_t offset) {}
	virtual void selectEncoderAction(int32_t offset) {}
	virtual void beginSession(MenuItem* navigatedBackwardFrom = nullptr){};
	virtual bool isRelevant(Sound* sound, int32_t whichThing) { return true; }
	virtual MenuItem* selectButtonPress() { return nullptr; }
	virtual MenuPermission checkPermissionToBeginSession(Sound* sound, int32_t whichThing, MultiRange** currentRange);
	virtual void readValueAgain() {}
	virtual bool selectEncoderActionEditsInstrument() { return false; }
	virtual uint8_t getPatchedParamIndex() { return 255; }
	virtual uint8_t getIndexOfPatchedParamToBlink() { return 255; }
	virtual uint8_t shouldDrawDotOnName() { return 255; }
	virtual uint8_t shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) { return 255; }

	virtual MenuItem* patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive = false) {
		return nullptr; // nullptr means do nothing. 0xFFFFFFFF means go up a level
	}

	virtual void unlearnAction() {}
	virtual bool allowsLearnMode() { return false; }
	virtual void learnKnob(MIDIDevice* fromDevice, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) {}
	virtual bool learnNoteOn(MIDIDevice* fromDevice, int32_t channel, int32_t noteCode) {
		return false;
	} // Returns whether it was used, I think?
	virtual void learnCC(MIDIDevice* fromDevice, int32_t channel, int32_t ccNumber, int32_t value);
	virtual bool shouldBlinkLearnLed() { return false; }
	virtual bool isRangeDependent() { return false; }
	virtual bool usesAffectEntire() { return false; }

	/// Can get overridden by getTitle(). Actual max num chars for OLED display is 14.
	deluge::string title;

	/// Get the title to be used when rendering on OLED. If not overriden, defaults to returning `title`.
	///
	/// The returned pointer must live long enough for us to draw the title, which for practical purposes means "the
	/// lifetime of this menu item"
	[[nodiscard]] virtual std::string_view getTitle() const { return title; }

	// OLED ONLY
	virtual void renderOLED();
	virtual void drawPixelsForOled() {}
	void drawItemsForOled(char const** options, int32_t selectedOption);

	template <size_t n>
	static void drawItemsForOled(deluge::static_vector<deluge::string, n>& options, int32_t selectedOption,
	                             int32_t offset = 0);
	/// Get the title to be used when rendering on OLED. If not overriden, defaults to returning `title`.
	virtual void drawName();
};

// A couple of our child classes call this - that's all
template <size_t n>
void MenuItem::drawItemsForOled(deluge::static_vector<deluge::string, n>& options, const int32_t selectedOption,
                                const int32_t offset) {
	int32_t baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;

	auto* it = std::next(options.begin(), offset); // fast-forward to the first option visible
	for (int32_t o = 0; o < OLED_HEIGHT_CHARS - 1 && o < options.size() - offset; o++) {
		int32_t yPixel = o * kTextSpacingY + baseY;

		OLED::drawString(options[o + offset].c_str(), kTextSpacingX, yPixel, OLED::oledMainImage[0],
		                 OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

		if (o == selectedOption) {
			OLED::invertArea(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8, &OLED::oledMainImage[0]);
			OLED::setupSideScroller(0, options[o + offset].c_str(), kTextSpacingX, OLED_MAIN_WIDTH_PIXELS, yPixel,
			                        yPixel + 8, kTextSpacingX, kTextSpacingY, true);
		}
	}
}
