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

#include "RZA1/system/r_typedefs.h"
#include "param.h"
#include "menu_item_with_cc_learning.h"

class ModelStackWithAutoParam;

namespace menu_item {

class PatchedParam : public Param, public MenuItemWithCCLearning {
public:
	PatchedParam() {}
	PatchedParam(int newP) : Param(newP) {}
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

} // namespace menu_item
