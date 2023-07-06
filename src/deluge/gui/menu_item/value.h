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

#include "menu_item.h"
#include "definitions.h"

namespace menu_item {

class Value : public MenuItem {
public:
	Value(char const* newName = NULL) : MenuItem(newName) {}
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

} // namespace menu_item
