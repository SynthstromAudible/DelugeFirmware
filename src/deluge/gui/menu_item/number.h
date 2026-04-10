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

enum RenderingStyle {
	NUMBER,
	KNOB,
	BAR,
	PERCENT,
	SLIDER,
	LENGTH_SLIDER,
	PAN,
	LPF,
	HPF,
	ATTACK,
	RELEASE,
	SIDECHAIN_DUCKING
};

class Number : public Value<int32_t> {
public:
	using Value::Value;
	void drawHorizontalBar(int32_t y_top, int32_t margin_l, int32_t margin_r = -1, int32_t height = 8);

protected:
	[[nodiscard]] virtual int32_t getMaxValue() const = 0;
	[[nodiscard]] virtual int32_t getMinValue() const { return 0; }
	[[nodiscard]] virtual RenderingStyle getRenderingStyle() const { return KNOB; }
	virtual float normalize(int32_t value);

	// Horizontal menus ------
	void renderInHorizontalMenu(const SlotPosition& slot) override;
	void drawKnob(const SlotPosition& slot);
	void drawBar(const SlotPosition& slot);
	void drawPercent(const SlotPosition& slot);
	void drawSlider(const SlotPosition& slot, std::optional<int32_t> value = std::nullopt);
	void drawLengthSlider(const SlotPosition& slot, bool min_slider_pos = 3);
	void drawPan(const SlotPosition& slot);
	void drawLpf(const SlotPosition& slot);
	void drawHpf(const SlotPosition& slot);
	void drawAttack(const SlotPosition& slot);
	void drawRelease(const SlotPosition& slot);
	void drawSidechainDucking(const SlotPosition& slot);
	void getNotificationValue(StringBuf& value) override;
};

} // namespace deluge::gui::menu_item
