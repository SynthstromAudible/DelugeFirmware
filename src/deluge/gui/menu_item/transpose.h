/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "decimal.h"
#include "definitions_cxx.hpp"
#include "gui/menu_item/patched_param.h"

namespace deluge::gui::menu_item {

class Transpose : public Decimal, public PatchedParam {
public:
	Transpose(const deluge::string &newName, int newP = 0)
	    : Decimal(newName), PatchedParam(newP) {}

	Transpose(const deluge::string &newName, const deluge::string &title, int newP = 0)
	    : Decimal(newName, title), PatchedParam(newP) {}

	MenuItem* selectButtonPress() final { return PatchedParam::selectButtonPress(); }
	[[nodiscard]] int getMinValue() const final { return -9600; }
	[[nodiscard]] int getMaxValue() const final { return 9600; }
	[[nodiscard]] int getNumDecimalPlaces() const final { return 2; }
	uint8_t getPatchedParamIndex() final { return PatchedParam::getPatchedParamIndex(); }
	uint8_t shouldDrawDotOnName() final { return PatchedParam::shouldDrawDotOnName(); }

#if !HAVE_OLED
	void drawValue() override {
		numericDriver.setTextAsNumber(this->value_, shouldDrawDotOnName());
	}
#endif

	uint8_t shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) final {
		return PatchedParam::shouldBlinkPatchingSourceShortcut(s, colour);
	}
	MenuItem* patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive = false) final {
		return PatchedParam::patchingSourceShortcutPress(s, previousPressStillActive);
	}

	void unlearnAction() final {
		MenuItemWithCCLearning::unlearnAction();
	}
	bool allowsLearnMode() final {
		return MenuItemWithCCLearning::allowsLearnMode();
	}
	void learnKnob(::MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) final {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};
};

} // namespace deluge::gui::menu_item
