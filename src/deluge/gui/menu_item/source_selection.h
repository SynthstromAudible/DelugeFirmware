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
#include "menu_item.h"
#include "definitions.h"

class ParamDescriptor;

namespace menu_item {
class SourceSelection : public MenuItem {
public:
	SourceSelection();
	void beginSession(MenuItem* navigatedBackwardFrom = NULL);
	void selectEncoderAction(int offset) final;
	virtual ParamDescriptor getDestinationDescriptor() = 0;
	uint8_t getIndexOfPatchedParamToBlink() final;
	uint8_t shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour) final;
	void readValueAgain() final;

#if HAVE_OLED
	void drawPixelsForOled();
	static int selectedRowOnScreen;
	int scrollPos; // Each instance needs to store this separately
#else
	void drawValue();
#endif

	uint8_t s;

protected:
	bool sourceIsAllowed(int source);
	uint8_t shouldDrawDotOnValue();
};
} // namespace menu_item
