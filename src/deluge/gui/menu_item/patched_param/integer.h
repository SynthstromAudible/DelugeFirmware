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
#include "gui/menu_item/patched_param.h"

namespace menu_item::patched_param {
class Integer : public PatchedParam, public menu_item::IntegerContinuous {
public:
	Integer(char const* newName = NULL, int newP = 0) : PatchedParam(newP), IntegerContinuous(newName) {}
#if !HAVE_OLED
	void drawValue() {
		PatchedParam::drawValue();
	}
#endif
	ParamDescriptor getLearningThing() final {
		return PatchedParam::getLearningThing();
	}
	int getMaxValue() const {
		return PatchedParam::getMaxValue();
	}
	int getMinValue() const {
		return PatchedParam::getMinValue();
	}
	uint8_t shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour) final {
		return PatchedParam::shouldBlinkPatchingSourceShortcut(s, colour);
	}

	uint8_t shouldDrawDotOnName() final {
		return PatchedParam::shouldDrawDotOnName();
	}
	MenuItem* selectButtonPress() final {
		return PatchedParam::selectButtonPress();
	}

	uint8_t getPatchedParamIndex() final {
		return PatchedParam::getPatchedParamIndex();
	}
	MenuItem* patchingSourceShortcutPress(int s, bool previousPressStillActive = false) final {
		return PatchedParam::patchingSourceShortcutPress(s, previousPressStillActive);
	}

	void unlearnAction() final {
		MenuItemWithCCLearning::unlearnAction();
	}
	bool allowsLearnMode() final {
		return MenuItemWithCCLearning::allowsLearnMode();
	}
	void learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) final {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};

protected:
	void readCurrentValue();
	void writeCurrentValue() final;
	virtual int32_t getFinalValue();
};
} // namespace menu_item::patched_param
