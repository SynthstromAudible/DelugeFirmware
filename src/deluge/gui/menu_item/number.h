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

enum NumberStyle { NUMBER, KNOB, VERTICAL_BAR, PERCENT, SLIDER, LENGTH_SLIDER };

class Number : public Value<int32_t> {
public:
	using Value::Value;
	void drawHorizontalBar(int32_t yTop, int32_t marginL, int32_t marginR = -1, int32_t height = 8);

protected:
	[[nodiscard]] virtual int32_t getMaxValue() const = 0;
	[[nodiscard]] virtual int32_t getMinValue() const { return 0; }
	[[nodiscard]] virtual NumberStyle getNumberStyle() const { return KNOB; }
	float getNormalizedValue();

	void renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) override;
	void drawKnob(int32_t startX, int32_t startY, int32_t width, int32_t height);
	void drawVerticalBar(int32_t startX, int32_t startY, int32_t slotWidth, int32_t slotHeight);
	void drawPercent(int32_t startX, int32_t startY, int32_t width, int32_t height);
	void drawSlider(int32_t startX, int32_t startY, int32_t slotWidth, int32_t slotHeight);
	void drawLengthSlider(int32_t startX, int32_t startY, int32_t slotWidth, int32_t slotHeight, bool minSliderPos = 3);
	void getNotificationValue(StringBuf& value);
};

} // namespace deluge::gui::menu_item
