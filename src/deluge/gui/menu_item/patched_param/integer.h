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
#include "definitions_cxx.hpp"
#include "gui/menu_item/patched_param.h"

namespace deluge::gui::menu_item::patched_param {
class Integer : public PatchedParam, public menu_item::IntegerContinuous {
public:
	Integer(l10n::String newName, int32_t newP = 0) : PatchedParam(newP), IntegerContinuous(newName) {}
	Integer(l10n::String newName, l10n::String title, int32_t newP = 0)
	    : PatchedParam(newP), IntegerContinuous(newName, title) {}
	// 7SEG Only
	void drawValue() override { display->setTextAsNumber(this->getValue(), shouldDrawDotOnName()); }

	ParamDescriptor getLearningThing() final { return PatchedParam::getLearningThing(); }
	[[nodiscard]] int32_t getMaxValue() const override { return PatchedParam::getMaxValue(); }
	[[nodiscard]] int32_t getMinValue() const override { return PatchedParam::getMinValue(); }
	uint8_t shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) final {
		return PatchedParam::shouldBlinkPatchingSourceShortcut(s, colour);
	}

	uint8_t shouldDrawDotOnName() final { return PatchedParam::shouldDrawDotOnName(); }
	MenuItem* selectButtonPress() final { return PatchedParam::selectButtonPress(); }

	uint8_t getPatchedParamIndex() final { return PatchedParam::getPatchedParamIndex(); }
	MenuItem* patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive = false) final {
		return PatchedParam::patchingSourceShortcutPress(s, previousPressStillActive);
	}

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDIDevice* fromDevice, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};

protected:
	void readCurrentValue() override;
	void writeCurrentValue() final;
	virtual int32_t getFinalValue();
};
} // namespace deluge::gui::menu_item::patched_param
