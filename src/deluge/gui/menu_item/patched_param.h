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

#include "definitions_cxx.hpp"
#include "menu_item_with_cc_learning.h"
#include "param.h"
#include <cstdint>

class ModelStackWithAutoParam;

namespace deluge::gui::menu_item {

class PatchedParam : public Param, public MenuItemWithCCLearning {
public:
	PatchedParam() = default;
	PatchedParam(int32_t newP) : Param(newP) {}
	MenuItem* selectButtonPress();
#if !HAVE_OLED
	virtual void drawValue() = 0;
#endif
	ParamDescriptor getLearningThing() override;
	virtual uint8_t getPatchedParamIndex();
	virtual uint8_t shouldDrawDotOnName();

	uint8_t shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour);
	MenuItem* patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive = false);
	ModelStackWithAutoParam* getModelStack(void* memory) override;

protected:
	ParamSet* getParamSet() override;
};

} // namespace deluge::gui::menu_item
