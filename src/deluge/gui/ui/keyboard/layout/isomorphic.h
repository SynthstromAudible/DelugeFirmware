/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "gui/ui/keyboard/layout.h"

namespace deluge::gui::ui::keyboard::layout {

constexpr int32_t kMinIsomorphicRowInterval = 1;
constexpr int32_t kMaxIsomorphicRowInterval = 16;

class KeyboardLayoutIsomorphic : public KeyboardLayout {
public:
	KeyboardLayoutIsomorphic() {}
	virtual ~KeyboardLayoutIsomorphic() {}

	virtual void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]);
	virtual void handleVerticalEncoder(int32_t offset);
	virtual void handleHorizontalEncoder(int32_t offset, bool shiftEnabled);
	virtual void precalculate();

	virtual void renderPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]);

	virtual char const* name() { return "Isomorphic"; }
	virtual bool supportsInstrument() { return true; }
	virtual bool supportsKit() { return false; }

private:
	inline uint8_t noteFromCoords(int32_t x, int32_t y) {
		return getState().isomorphic.scrollOffset + x + y * getState().isomorphic.rowInterval;
	}

	uint8_t noteColours[kDisplayHeight * kMaxIsomorphicRowInterval + kDisplayWidth][3];
};

}; // namespace deluge::gui::ui::keyboard::layout
