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
	void selectEncoderAction(int32_t offset) final;
	void horizontalEncoderAction(int32_t offset) override;
	void renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) override;
	void setupNumberEditor() override;
	/// How getValue() relates to displayed number? Default is divide by 100.
	[[nodiscard]] virtual int32_t getEditorScale() const { return 100; }

protected:
	void drawValue() override;
	[[nodiscard]] virtual int32_t getNumDecimalPlaces() const = 0;
	[[nodiscard]] virtual int32_t getDefaultEditPos() const { return 2; }

	void drawPixelsForOled() override;

	// 7Seg Only
	virtual void drawActualValue(bool justDidHorizontalScroll = false);

private:
	void scrollToGoodPos();
};

} // namespace deluge::gui::menu_item
