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
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "util/sized.h"
#include <cstdint>
#include <span>
#include <vector>

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
	MenuItem() : name(deluge::l10n::String::EMPTY_STRING), title(deluge::l10n::String::EMPTY_STRING) {}
	MenuItem(deluge::l10n::String newName, deluge::l10n::String newTitle = deluge::l10n::String::EMPTY_STRING)
	    : name(newName), title(newTitle) {
		if (newTitle == deluge::l10n::String::EMPTY_STRING) {
			title = newName;
		}
	}

	MenuItem(const MenuItem& other) = delete;
	MenuItem(const MenuItem&& other) = delete;
	MenuItem& operator=(const MenuItem& other) = delete;
	MenuItem& operator=(const MenuItem&& other) = delete;

	virtual ~MenuItem() = default;

	/// As viewed in a menu list. For OLED, up to 20 chars.
	const deluge::l10n::String name;
	[[nodiscard]] virtual std::string_view getName() const { return deluge::l10n::getView(name); }

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
	virtual void learnProgramChange(MIDIDevice* fromDevice, int32_t channel, int32_t programNumber) {}
	virtual void learnCC(MIDIDevice* fromDevice, int32_t channel, int32_t ccNumber, int32_t value);
	virtual bool shouldBlinkLearnLed() { return false; }
	virtual bool isRangeDependent() { return false; }
	virtual bool usesAffectEntire() { return false; }

	virtual ActionResult timerCallback() { return ActionResult::DEALT_WITH; }

	/// Can get overridden by getTitle(). Actual max num chars for OLED display is 14.
	deluge::l10n::String title;

	/// Get the title to be used when rendering on OLED. If not overriden, defaults to returning `title`.
	///
	/// The returned pointer must live long enough for us to draw the title, which for practical purposes means "the
	/// lifetime of this menu item"
	[[nodiscard]] virtual std::string_view getTitle() const { return deluge::l10n::getView(title); }

	virtual void renderOLED();
	virtual void drawPixelsForOled() {}

	static void drawItemsForOled(std::span<std::string_view> options, int32_t selectedOption, int32_t offset = 0);
	/// Get the title to be used when rendering on OLED. If not overriden, defaults to returning `title`.
	virtual void drawName();
};
