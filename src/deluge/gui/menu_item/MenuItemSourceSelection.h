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

#ifndef MENUITEMSOURCESELECTION_H_
#define MENUITEMSOURCESELECTION_H_

#include "MenuItem.h"
#include "definitions.h"

class MenuItemSourceSelection : public MenuItem {
public:
	MenuItemSourceSelection();
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

class MenuItemSourceSelectionRegular final : public MenuItemSourceSelection {
public:
	MenuItemSourceSelectionRegular();
	void beginSession(MenuItem* navigatedBackwardFrom = NULL);
	ParamDescriptor getDestinationDescriptor();
	MenuItem* selectButtonPress();
	MenuItem* patchingSourceShortcutPress(int newS, bool previousPressStillActive);
};

class MenuItemSourceSelectionRange final : public MenuItemSourceSelection {
public:
	MenuItemSourceSelectionRange();
	ParamDescriptor getDestinationDescriptor();
	MenuItem* selectButtonPress();
	MenuItem* patchingSourceShortcutPress(int newS, bool previousPressStillActive);
};

extern MenuItemSourceSelectionRegular sourceSelectionMenuRegular;
extern MenuItemSourceSelectionRange sourceSelectionMenuRange;

#endif /* MENUITEMSOURCESELECTION_H_ */
