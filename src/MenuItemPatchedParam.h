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

#ifndef MENUITEMPATCHEDPARAM_H_
#define MENUITEMPATCHEDPARAM_H_
#include "r_typedefs.h"
#include "MenuItemParam.h"
#include "MenuItemWithCCLearning.h"

class ModelStackWithAutoParam;

class MenuItemPatchedParam : public MenuItemParam, public MenuItemWithCCLearning {
public:
	MenuItemPatchedParam() {}
	MenuItemPatchedParam(int newP) : MenuItemParam(newP) {}
	MenuItem* selectButtonPress();
#if !HAVE_OLED
	void drawValue();
#endif
	ParamDescriptor getLearningThing();
	uint8_t getPatchedParamIndex();
	uint8_t shouldDrawDotOnName();

	uint8_t shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour);
	MenuItem* patchingSourceShortcutPress(int s, bool previousPressStillActive = false);
	ModelStackWithAutoParam* getModelStack(void* memory);

protected:
	ParamSet* getParamSet();
};

class MenuItemPatchedParamInteger : public MenuItemPatchedParam, public MenuItemIntegerContinuous {
public:
	MenuItemPatchedParamInteger(char const* newName = NULL, int newP = 0)
	    : MenuItemPatchedParam(newP), MenuItemIntegerContinuous(newName) {}
#if !HAVE_OLED
	void drawValue() {
		MenuItemPatchedParam::drawValue();
	}
#endif
	ParamDescriptor getLearningThing() final {
		return MenuItemPatchedParam::getLearningThing();
	}
	int getMaxValue() {
		return MenuItemPatchedParam::getMaxValue();
	}
	int getMinValue() {
		return MenuItemPatchedParam::getMinValue();
	}
	uint8_t shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour) final {
		return MenuItemPatchedParam::shouldBlinkPatchingSourceShortcut(s, colour);
	}

	uint8_t shouldDrawDotOnName() final {
		return MenuItemPatchedParam::shouldDrawDotOnName();
	}
	MenuItem* selectButtonPress() final {
		return MenuItemPatchedParam::selectButtonPress();
	}

	uint8_t getPatchedParamIndex() final {
		return MenuItemPatchedParam::getPatchedParamIndex();
	}
	MenuItem* patchingSourceShortcutPress(int s, bool previousPressStillActive = false) final {
		return MenuItemPatchedParam::patchingSourceShortcutPress(s, previousPressStillActive);
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

class MenuItemSourceDependentPatchedParam : public MenuItemPatchedParamInteger {
public:
	MenuItemSourceDependentPatchedParam(char const* newName = 0, int newP = 0)
	    : MenuItemPatchedParamInteger(newName, newP) {}
	uint8_t getP();
};

class MenuItemPatchedParamPan : public MenuItemPatchedParamInteger {
public:
	MenuItemPatchedParamPan(char const* newName = 0, int newP = 0) : MenuItemPatchedParamInteger(newName, newP) {}
#if !HAVE_OLED
	void drawValue();
#endif
protected:
	int getMaxValue() {
		return 32;
	}
	int getMinValue() {
		return -32;
	}
	int32_t getFinalValue();
	void readCurrentValue();
};

#endif /* MENUITEMPATCHEDPARAM_H_ */
