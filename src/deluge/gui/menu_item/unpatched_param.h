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

#pragma once

#include "menu_item_with_cc_learning.h"
#include "param.h"

class ModelStackWithAutoParam;

namespace deluge::gui::menu_item {

class UnpatchedParam : public Param, public IntegerContinuous, public MenuItemWithCCLearning {
public:
	UnpatchedParam(const string& newName, const string& title, int32_t newP)
	    : Param(newP), IntegerContinuous(newName, title) {}

	UnpatchedParam(const string& newName, int32_t newP) : Param(newP), IntegerContinuous(newName) {}

	void readCurrentValue() override;
	void writeCurrentValue() override;
	ParamDescriptor getLearningThing() final;
	[[nodiscard]] int32_t getMaxValue() const override { return Param::getMaxValue(); }
	[[nodiscard]] int32_t getMinValue() const override { return Param::getMinValue(); }
	MenuItem* selectButtonPress() final { return Param::selectButtonPress(); }

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDIDevice* fromDevice, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};

	ParamSet* getParamSet() final;
	ModelStackWithAutoParam* getModelStack(void* memory) final;

protected:
	virtual int32_t getFinalValue();
};

} // namespace deluge::gui::menu_item
