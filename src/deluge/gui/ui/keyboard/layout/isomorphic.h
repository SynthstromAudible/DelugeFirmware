/*
 * Copyright © 2016-2024 Synthstrom Audible Limited
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

#include "gui/ui/keyboard/layout/column_controls.h"

namespace deluge::gui::ui::keyboard::layout {

class KeyboardLayoutIsomorphic : public ColumnControlsKeyboard {
public:
	KeyboardLayoutIsomorphic() {}
	~KeyboardLayoutIsomorphic() override {}

	void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) override;
	void handleVerticalEncoder(int32_t offset) override;
	void handleHorizontalEncoder(int32_t offset, bool shiftEnabled) override;
	void precalculate() override;

	void renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) override;

	char const* name() override { return "Isomorphic"; }
	bool supportsInstrument() override { return true; }
	bool supportsKit() override { return false; }

private:
	void offsetPads(int32_t offset, bool shiftEnabled);
	inline uint8_t noteFromCoords(int32_t x, int32_t y) {
		return getState().isomorphic.scrollOffset + x + y * getState().isomorphic.rowInterval;
	}

	RGB noteColours[kDisplayHeight * kMaxIsomorphicRowInterval + kDisplayWidth];
};

}; // namespace deluge::gui::ui::keyboard::layout
