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
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/display/seven_segment.h"
#include <version.h>

namespace deluge::gui::menu_item::firmware {
class Version final : public MenuItem {
public:
	using MenuItem::MenuItem;

	void drawPixelsForOled() {
		deluge::hid::display::OLED::drawStringCentredShrinkIfNecessary(
		    kFirmwareVersionString, 22, deluge::hid::display::OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, 18, 20);
	}

	void beginSession(MenuItem* navigatedBackwardFrom) override { drawValue(); }

	void drawValue() {
		if (display->have7SEG()) {
			static_cast<hid::display::SevenSegment*>(display)->enableLowercase();
		}
		display->setScrollingText(kFirmwareVersionString);
		if (display->have7SEG()) {
			static_cast<hid::display::SevenSegment*>(display)->disableLowercase();
		}
	}
};
} // namespace deluge::gui::menu_item::firmware
