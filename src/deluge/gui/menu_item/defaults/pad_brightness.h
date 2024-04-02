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
#include "gui/menu_item/integer.h"
#include "hid/led/pad_leds.h"
#include "storage/flash_storage.h"

namespace deluge::gui::menu_item::defaults {
class PadBrightness final : public Integer {
public:
	using Integer::Integer;
	void selectEncoderAction(int32_t offset) override {
		offset = offset * 4;
		Integer::selectEncoderAction(offset);
	}
	[[nodiscard]] int32_t getMinValue() const override { return to_ui(kMinLedBrightness); }
	[[nodiscard]] int32_t getMaxValue() const override { return to_ui(kMaxLedBrightness); }
	void readCurrentValue() override { this->setValue(to_ui(FlashStorage::defaultPadBrightness)); }
	void writeCurrentValue() override {
		int32_t normalizedValue = to_internal(this->getValue());
		FlashStorage::defaultPadBrightness = normalizedValue;
		PadLEDs::setBrightnessLevel(normalizedValue);
	}

private:
	int32_t to_internal(int32_t value) const { return value >> 2; }
	int32_t to_ui(int32_t value) const { return value << 2; }
};
} // namespace deluge::gui::menu_item::defaults
