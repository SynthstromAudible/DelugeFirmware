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

#include "gui/ui/ui.h"
#include "hid/display/display.h"
#include "menu_item.h"
#include "util/misc.h"

#include <hid/buttons.h>

namespace deluge::gui::menu_item {
template <typename T = int32_t>
class Value : public MenuItem {
public:
	using MenuItem::MenuItem;
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void selectEncoderAction(int32_t offset) override;
	void readValueAgain() override;
	bool selectEncoderActionEditsInstrument() final { return true; }

	void setValue(T value) { value_ = value; }

	template <util::enumeration E>
	void setValue(E value) {
		value_ = util::to_underlying(value);
	}

	T getValue() { return value_; }

	template <util::enumeration E>
	E getValue() {
		return static_cast<E>(value_);
	}

protected:
	virtual void writeCurrentValue() {}

	// 7SEG ONLY
	virtual void drawValue() = 0;

private:
	T value_;
};

template <typename T>
void Value<T>::beginSession(MenuItem* navigatedBackwardFrom) {
	if (display->haveOLED()) {
		readCurrentValue();
	}
	else {
		readValueAgain();
	}
}

template <typename T>
void Value<T>::selectEncoderAction(int32_t offset) {
	if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
		Buttons::selectButtonPressUsedUp = true;
	}

	writeCurrentValue();

	// For MenuItems referring to an AutoParam (so UnpatchedParam and PatchedParam), ideally we wouldn't want to render
	// the display here, because that'll happen soon anyway due to a setting of TIMER_DISPLAY_AUTOMATION.
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue(); // Probably not necessary either...
	}
}

template <typename T>
void Value<T>::readValueAgain() {
	readCurrentValue();
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}
}

} // namespace deluge::gui::menu_item
