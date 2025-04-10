/*
 * Copyright (c) 2024 Sean Ditny
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
#include "model/song/song.h"
#include "storage/flash_storage.h"

namespace deluge::gui::menu_item::defaults {
class HoldTime final : public Integer {
public:
	using Integer::Integer;
	[[nodiscard]] int32_t getMinValue() const override { return 1; }
	[[nodiscard]] int32_t getMaxValue() const override { return 20; }
	void readCurrentValue() override { this->setValue(FlashStorage::defaultHoldTime); }
	void writeCurrentValue() override {
		FlashStorage::defaultHoldTime = this->getValue();
		FlashStorage::holdTime = (FlashStorage::defaultHoldTime * kSampleRate) / 20;
	}
	int32_t getDisplayValue() override {
		int32_t currentValue = FlashStorage::defaultHoldTime;
		if (currentValue == 20) {
			return 1;
		}
		else {
			return currentValue * 50;
		}
	}
	const char* getUnit() override {
		if (FlashStorage::defaultHoldTime == 20) {
			return " SEC";
		}
		else {
			return " MS";
		}
	}
};
} // namespace deluge::gui::menu_item::defaults
