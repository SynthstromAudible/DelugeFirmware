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
constexpr int32_t kVelModShift = 22;

class KeyboardLayoutIsomorphic : public KeyboardLayout {
public:
	KeyboardLayoutIsomorphic() {}
	~KeyboardLayoutIsomorphic() override {}

	void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) override;
	void handleVerticalEncoder(int32_t offset) override;
	void handleHorizontalEncoder(int32_t offset, bool shiftEnabled) override;
	void precalculate() override;

	void renderPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]) override;
	void renderSidebarPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]) override;

	char const* name() override { return "Isomorphic"; }
	bool supportsInstrument() override { return true; }
	bool supportsKit() override { return false; }

private:
	inline uint8_t noteFromCoords(int32_t x, int32_t y) {
		return getState().isomorphic.scrollOffset + x + y * getState().isomorphic.rowInterval;
	}

	uint8_t noteColours[kDisplayHeight * kMaxIsomorphicRowInterval + kDisplayWidth][3];

	// use higher precision internally so that scaling and stepping is cleaner
	int32_t velocityMax = 127 << kVelModShift;
	int32_t velocityMin = 15 << kVelModShift;
	uint32_t velocityStep = 16 << kVelModShift;
	uint32_t velocity32 = 64 << kVelModShift;
	uint8_t velocity = 64;

	int32_t modMax = 127 << kVelModShift;
	int32_t modMin = 15 << kVelModShift;
	uint32_t modStep = 16 << kVelModShift;
	uint32_t mod32 = 0 << kVelModShift;

	bool velocityMinHeld = false;
	bool velocityMaxHeld = false;

	bool modMinHeld = false;
	bool modMaxHeld = false;
};

}; // namespace deluge::gui::ui::keyboard::layout
