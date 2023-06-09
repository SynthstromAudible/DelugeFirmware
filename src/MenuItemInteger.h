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

#ifndef MENUITEMINTEGER_H_
#define MENUITEMINTEGER_H_

#include "MenuItemNumber.h"

class MenuItemInteger : public MenuItemNumber {
public:
	MenuItemInteger(char const* newName = NULL) : MenuItemNumber(newName) {}
	void selectEncoderAction(int offset);

protected:
#if HAVE_OLED
	void drawPixelsForOled();
	virtual void drawInteger(int textWidth, int textHeight, int yPixel);
#else
	void drawValue();
#endif
};

class MenuItemIntegerWithOff : public MenuItemInteger {
public:
	MenuItemIntegerWithOff(char const* newName = NULL) : MenuItemInteger(newName) {}
#if !HAVE_OLED
	void drawValue();
#endif
};

class MenuItemIntegerContinuous : public MenuItemInteger {
public:
	MenuItemIntegerContinuous(char const* newName = NULL) : MenuItemInteger(newName) {}

protected:
#if HAVE_OLED
	void drawPixelsForOled();
	void drawBar(int yTop, int marginL, int marginR = -1);
#endif
};

#endif /* MENUITEMINTEGER_H_ */
