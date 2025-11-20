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

#include "number.h"

namespace deluge::gui::menu_item {

class Decimal : public Number {
public:
	using Number::Number;
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	void selectEncoderAction(int32_t offset) override;
	void horizontalEncoderAction(int32_t offset) override;
	void renderInHorizontalMenu(const SlotPosition& slot) override;
	void getNotificationValue(StringBuf& valueBuf) override { valueBuf.appendFloat(getValue() / 100.f, 2, 2); }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return NUMBER; }

protected:
	void drawValue() override;
	[[nodiscard]] virtual int32_t getNumDecimalPlaces() const = 0;
	[[nodiscard]] virtual int32_t getDefaultEditPos() const { return 2; }
	[[nodiscard]] virtual int32_t getNumberEditSize();

	void drawPixelsForOled() override;

	// 7Seg Only
	virtual void drawActualValue(bool justDidHorizontalScroll = false);
	virtual void appendAdditionalDots(std::vector<uint8_t>& dotPositions) {};
	static int32_t getNumNonZeroDecimals(int32_t value);

private:
	void scrollToGoodPos();
};

class DecimalWithoutScrolling : public Decimal {
	using Decimal::Decimal;

public:
	void selectEncoderAction(int32_t offset) override;
	void horizontalEncoderAction(int32_t offset) override { return; }

protected:
	virtual float getDisplayValue() { return this->getValue(); }
	void getNotificationValue(StringBuf& value) override;
	virtual const char* getUnit() { return ""; }
	void drawPixelsForOled() override;
	void drawDecimal(int32_t textWidth, int32_t textHeight, int32_t yPixel);
	// 7Seg Only
	void drawActualValue(bool justDidHorizontalScroll = false) override;
};

} // namespace deluge::gui::menu_item
