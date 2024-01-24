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

#include "value.h"
#include <cstdint>

namespace deluge::gui::menu_item {
class Number : public Value<int32_t> {
public:
	using Value::Value;
	void drawBar(int32_t yTop, int32_t marginL, int32_t marginR = -1);

protected:
	[[nodiscard]] virtual int32_t getMaxValue() const = 0;
	[[nodiscard]] virtual int32_t getMinValue() const { return 0; }
};

} // namespace deluge::gui::menu_item
