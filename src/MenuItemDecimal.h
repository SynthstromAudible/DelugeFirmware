/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#ifndef MENUITEMDECIMAL_H_
#define MENUITEMDECIMAL_H_

#include "MenuItemNumber.h"
#include "MenuItemPatchedParam.h"
#include "MenuItemWithCCLearning.h"

class MenuItemDecimal : public MenuItemNumber {
public:
	MenuItemDecimal(char const* newName = NULL);
	void beginSession(MenuItem* navigatedBackwardFrom = NULL);
	void selectEncoderAction(int offset) final;
	void horizontalEncoderAction(int offset);
	int basicNumDecimalPlaces;
	int basicDefaultEditPos;

protected:
	void drawValue();
	virtual int getNumDecimalPlaces();
	virtual int getDefaultEditPos() { return basicDefaultEditPos; }
#if HAVE_OLED
	void drawPixelsForOled();
#else
	virtual void drawActualValue(bool justDidHorizontalScroll = false);
#endif

private:
	void scrollToGoodPos();
};

class MenuItemTranspose : public MenuItemDecimal, public MenuItemPatchedParam {
public:
	MenuItemTranspose(char const* newName = NULL, int newP = 0)
	    : MenuItemPatchedParam(newP), MenuItemDecimal(newName) {}
	MenuItem* selectButtonPress() final { return MenuItemPatchedParam::selectButtonPress(); }
	int getMinValue() final { return -9600; }
	int getMaxValue() final { return 9600; }
	int getNumDecimalPlaces() final { return 2; }
	uint8_t getPatchedParamIndex() final { return MenuItemPatchedParam::getPatchedParamIndex(); }
	uint8_t shouldDrawDotOnName() final { return MenuItemPatchedParam::shouldDrawDotOnName(); }
	uint8_t shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour) final {
		return MenuItemPatchedParam::shouldBlinkPatchingSourceShortcut(s, colour);
	}
	MenuItem* patchingSourceShortcutPress(int s, bool previousPressStillActive = false) final {
		return MenuItemPatchedParam::patchingSourceShortcutPress(s, previousPressStillActive);
	}

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) final {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};
};

class MenuItemSourceDependentTranspose : public MenuItemTranspose {
public:
	MenuItemSourceDependentTranspose(char const* newName = NULL, int newP = 0) : MenuItemTranspose(newName, newP) {}
	ParamDescriptor getLearningThing() final {
		ParamDescriptor paramDescriptor;
		paramDescriptor.setToHaveParamOnly(getP());
		return paramDescriptor;
	}

protected:
	uint8_t getP() final;
};

#endif /* MENUITEMDECIMAL_H_ */
