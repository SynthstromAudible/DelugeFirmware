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
	Transpose(const std::string& newName, int32_t newP = 0) : Decimal(newName), PatchedParam(newP) {}

	Transpose(const std::string& newName, const std::string& title, int32_t newP = 0)
	    : Decimal(newName, title), PatchedParam(newP) {}

	MenuItem* selectButtonPress() final { return PatchedParam::selectButtonPress(); }
	[[nodiscard]] int32_t getMinValue() const final { return -9600; }
	[[nodiscard]] int32_t getMaxValue() const final { return 9600; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const final { return 2; }
	uint8_t getPatchedParamIndex() final { return PatchedParam::getPatchedParamIndex(); }
	uint8_t shouldDrawDotOnName() final { return PatchedParam::shouldDrawDotOnName(); }

#if !HAVE_OLED
	void drawValue() override {
		numericDriver.setTextAsNumber(this->getValue(), shouldDrawDotOnName());
	}
#endif

	uint8_t shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* color) final {
		return PatchedParam::shouldBlinkPatchingSourceShortcut(s, color);
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
	void learnKnob(::MIDIDevice* fromDevice, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};
};

} // namespace deluge::gui::menu_item
