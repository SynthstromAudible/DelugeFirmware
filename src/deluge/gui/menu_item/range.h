/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "gui/menu_item/value.h"

namespace deluge::gui::menu_item {

enum class RangeEdit : uint8_t {
	OFF = 0,
	LEFT = 1,
	RIGHT = 2,
};

class Range : public Value<int32_t> {
public:
	using Value::Value;

	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void horizontalEncoderAction(int32_t offset) final;
	bool cancelEditingIfItsOn();

protected:
	virtual void getText(char* buffer, int32_t* getLeftLength = nullptr, int32_t* getRightLength = nullptr,
	                     bool mayShowJustOne = true) = 0;
	virtual bool mayEditRangeEdge(RangeEdit whichEdge) { return true; }
	virtual void drawValue() { this->drawValue(0); }
	void drawValue(int32_t startPos, bool renderSidebarToo = true);
	void drawValueForEditingRange(bool blinkImmediately);

	// OLED ONLY
	void drawPixelsForOled();
};
} // namespace deluge::gui::menu_item
