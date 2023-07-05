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

#include "number.h"
#include "patched_param.h"

namespace menu_item {

class Decimal : public Number {
public:
	Decimal(char const* newName = NULL) : Number(newName) {}
	void beginSession(MenuItem* navigatedBackwardFrom = NULL);
	void selectEncoderAction(int offset) final;
	void horizontalEncoderAction(int offset);

protected:
	void drawValue();
	virtual int getNumDecimalPlaces() const = 0;
	virtual int getDefaultEditPos() const { return 2; }
#if HAVE_OLED
	void drawPixelsForOled();
#else
	virtual void drawActualValue(bool justDidHorizontalScroll = false);
#endif

private:
	void scrollToGoodPos();
};

} // namespace menu_item
