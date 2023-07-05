/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "gui/menu_item/selection.h"
#include "hid/display/numeric_driver.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"

extern char const* firmwareString;

namespace menu_item::firmware {
class Version final : public MenuItem {
public:
	using MenuItem::MenuItem;

#if HAVE_OLED
	void drawPixelsForOled() {
		OLED::drawStringCentredShrinkIfNecessary(firmwareString, 22, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, 18,
		                                         20);
	}
#else
	void beginSession(MenuItem* navigatedBackwardFrom) {
		drawValue();
	}

	void drawValue() {
		numericDriver.setScrollingText(firmwareString);
	}
#endif
};
} // namespace menu_item::firmware
