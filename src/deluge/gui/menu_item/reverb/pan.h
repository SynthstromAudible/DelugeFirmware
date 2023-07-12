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
#include <cstring>
#include <cmath>

#include "gui/menu_item/integer.h"

#include "gui/ui/sound_editor.h"
#include "util/cfunctions.h"
#include "hid/display/numeric_driver.h"

#include "processing/engines/audio_engine.h"

namespace deluge::gui::menu_item::reverb {
class Pan final : public Integer {
public:
	using Integer::Integer;
	virtual void drawValue() {
		char buffer[5];
		intToString(std::abs(this->value_), buffer, 1);
		if (this->value_ < 0) {
			strcat(buffer, "L");
		}
		else if (this->value_ > 0) {
			strcat(buffer, "R");
		}
		numericDriver.setText(buffer, true);
	}

	void writeCurrentValue() override { AudioEngine::reverbPan = ((int32_t)this->value_ * 33554432); }

	void readCurrentValue() override { this->value_ = ((int64_t)AudioEngine::reverbPan * 128 + 2147483648) >> 32; }
	[[nodiscard]] int getMaxValue() const override { return 32; }
	[[nodiscard]] int getMinValue() const override { return -32; }
};
} // namespace deluge::gui::menu_item::reverb
