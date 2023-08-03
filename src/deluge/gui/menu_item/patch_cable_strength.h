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
#include "decimal.h"
#include "menu_item_with_cc_learning.h"

namespace deluge::gui::menu_item {

class PatchCableStrength : public Decimal, public MenuItemWithCCLearning {
public:
	using Decimal::Decimal;
	void beginSession(MenuItem* navigatedBackwardFrom) final;
	void readCurrentValue() final;
	void writeCurrentValue() override;
	[[nodiscard]] int32_t getMinValue() const final { return -5000; }
	[[nodiscard]] int32_t getMaxValue() const final { return 5000; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const final { return 2; }
	virtual int32_t getDefaultEditPos() { return 2; }
	MenuPermission checkPermissionToBeginSession(Sound* sound, int32_t whichThing, MultiRange** currentRange) override;
	virtual ParamDescriptor getDestinationDescriptor() = 0;
	virtual PatchSource getS() = 0;
	uint8_t getIndexOfPatchedParamToBlink() final;
	MenuItem* selectButtonPress() override;

	//OLED Only
	void renderOLED();

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDIDevice* fromDevice, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};

protected:
	bool preferBarDrawing = false;
	ModelStackWithAutoParam* getModelStack(void* memory, bool allowCreation = false);
};

} // namespace deluge::gui::menu_item
