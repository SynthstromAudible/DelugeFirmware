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

#include "enumeration.h"
#include "gui/menu_item/enumeration.h"
#include "gui/menu_item/menu_item.h"
#include "util/sized.h"
#include "util/container/static_vector.hpp"

#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"
#include "hid/display/oled.h"

extern "C" {
#if HAVE_OLED
#include "RZA1/oled/oled_low_level.h"
#endif
}

namespace deluge::gui::menu_item {
template <size_t n>
class Selection : public Enumeration<n> {
public:
	using Enumeration<n>::Enumeration;

	virtual static_vector<char const*, n> getOptions() = 0;

	void drawValue() override;

#if HAVE_OLED
	void drawPixelsForOled() override;
#endif
	size_t size() override {
		return this->getOptions().size();
	}
	constexpr static size_t capacity() {
		return n;
	}
};

template <size_t n>
void Selection<n>::drawValue() {
#if HAVE_OLED
	renderUIsForOled();
#else
	const auto options = getOptions();
	numericDriver.setText(options[this->value_]);
#endif
}

#if HAVE_OLED

template <size_t n>
void Selection<n>::drawPixelsForOled() {
	// Move scroll
	if (soundEditor.menuCurrentScroll > this->value_) {
		soundEditor.menuCurrentScroll = this->value_;
	}
	else if (soundEditor.menuCurrentScroll < this->value_ - OLED_MENU_NUM_OPTIONS_VISIBLE + 1) {
		soundEditor.menuCurrentScroll = this->value_ - OLED_MENU_NUM_OPTIONS_VISIBLE + 1;
	}

	const int selectedOption = this->value_ - soundEditor.menuCurrentScroll;

	auto options = getOptions();
	MenuItem::drawItemsForOled(options, selectedOption, soundEditor.menuCurrentScroll);
}
#endif

} // namespace deluge::gui::menu_item
