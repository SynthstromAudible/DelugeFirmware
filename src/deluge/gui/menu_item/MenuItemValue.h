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

#ifndef MENUITEMVALUE_H_
#define MENUITEMVALUE_H_

#include "MenuItem.h"
#include "definitions.h"

class MenuItemValue : public MenuItem {
public:
	MenuItemValue(char const* newName = NULL) : MenuItem(newName) {}
	void beginSession(MenuItem* navigatedBackwardFrom);
	void selectEncoderAction(int offset);
	void readValueAgain() final;
	bool selectEncoderActionEditsInstrument() final { return true; }

protected:
	virtual void readCurrentValue() {}
	virtual void writeCurrentValue() {}
#if !HAVE_OLED
	virtual void drawValue() = 0;
#endif
};

#endif /* MENUITEMVALUE_H_ */
