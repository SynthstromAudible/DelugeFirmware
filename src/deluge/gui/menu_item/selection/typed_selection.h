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

#include "gui/menu_item/enumeration/typed_enumeration.h"
#include "gui/menu_item/menu_item.h"
#include "util/container/static_vector.hpp"
#include "util/misc.h"
#include "util/sized.h"

#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"
#include "hid/display/oled.h"

extern "C" {
#if HAVE_OLED
#include "RZA1/oled/oled_low_level.h"
#endif
}

namespace deluge::gui::menu_item {
template <util::enumeration T, size_t n>
class TypedSelection : public TypedEnumeration<T, n> {
public:
	using TypedEnumeration<T, n>::TypedEnumeration;

	virtual static_vector<string, n> getOptions() = 0;

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

template <util::enumeration T, size_t n>
void TypedSelection<T, n>::drawValue() {
#if HAVE_OLED
	renderUIsForOled();
#else
	const auto options = getOptions();
	auto idx = util::to_underlying(this->value_);
	numericDriver.setText(options[idx].c_str());
#endif
}

#if HAVE_OLED

template <util::enumeration T, size_t n>
void TypedSelection<T, n>::drawPixelsForOled() {
	auto current = util::to_underlying(this->value_);
	// Move scroll
	if (soundEditor.menuCurrentScroll > current) {
		soundEditor.menuCurrentScroll = current;
	}
	else if (soundEditor.menuCurrentScroll < current - kOLEDMenuNumOptionsVisible + 1) {
		soundEditor.menuCurrentScroll = current - kOLEDMenuNumOptionsVisible + 1;
	}

	const int32_t selectedOption = current - soundEditor.menuCurrentScroll;

	auto options = getOptions();
	MenuItem::drawItemsForOled(options, selectedOption, soundEditor.menuCurrentScroll);
}
#endif

} // namespace deluge::gui::menu_item
