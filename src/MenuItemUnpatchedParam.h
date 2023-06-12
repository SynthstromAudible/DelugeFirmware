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

#ifndef MENUITEMUNPATCHEDPARAM_H_
#define MENUITEMUNPATCHEDPARAM_H_

#include "MenuItemParam.h"
#include "MenuItemWithCCLearning.h"

class ModelStackWithAutoParam;

class MenuItemUnpatchedParam : public MenuItemParam, public MenuItemIntegerContinuous, public MenuItemWithCCLearning {
public:
	MenuItemUnpatchedParam();
	MenuItemUnpatchedParam(char const* newName, int newP) : MenuItemParam(newP), MenuItemIntegerContinuous(newName) {}

	void readCurrentValue();
	void writeCurrentValue();
	ParamDescriptor getLearningThing() final;
	int getMaxValue() { return MenuItemParam::getMaxValue(); }
	int getMinValue() { return MenuItemParam::getMinValue(); }
	MenuItem* selectButtonPress() final { return MenuItemParam::selectButtonPress(); }

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) final {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};

	ParamSet* getParamSet() final;
	ModelStackWithAutoParam* getModelStack(void* memory) final;

protected:
	virtual int32_t getFinalValue();
};

class MenuItemUnpatchedParamPan final : public MenuItemUnpatchedParam {
public:
	MenuItemUnpatchedParamPan(char const* newName = 0, int newP = 0) : MenuItemUnpatchedParam(newName, newP) {}
	void drawValue();

protected:
	int getMaxValue() { return 32; }
	int getMinValue() { return -32; }
	int32_t getFinalValue();
	void readCurrentValue();
};

class MenuItemUnpatchedParamUpdatingReverbParams final : public MenuItemUnpatchedParam {
public:
	MenuItemUnpatchedParamUpdatingReverbParams(char const* newName = 0, int newP = 0)
	    : MenuItemUnpatchedParam(newName, newP) {}
	void writeCurrentValue();
};

#endif /* MENUITEMUNPATCHEDPARAM_H_ */
