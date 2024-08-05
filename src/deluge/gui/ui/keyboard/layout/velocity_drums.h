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

constexpr int32_t kMinDrumPadEdgeSize = 1;
constexpr int32_t kMaxDrumPadEdgeSize = 8;

class KeyboardLayoutVelocityDrums : KeyboardLayout {
public:
	KeyboardLayoutVelocityDrums() {}
	~KeyboardLayoutVelocityDrums() override {}

	void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) override;
	void handleVerticalEncoder(int32_t offset) override;
	void handleHorizontalEncoder(int32_t offset, bool shiftEnabled, PressedPad presses[kMaxNumKeyboardPadPresses], bool encoderPressed = false) override;
	void precalculate() override;

	void renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) override;

	char const* name() override { return "Drums"; }
	bool supportsInstrument() override { return false; }
	bool supportsKit() override { return true; }

private:
	inline uint8_t noteFromCoords(int32_t x, int32_t y) {
		uint8_t edgeSize = (uint32_t)getState().drums.edgeSize;
		uint8_t padsPerRow = kDisplayWidth / edgeSize;
		return (x / edgeSize) + ((y / edgeSize) * padsPerRow) + getState().drums.scrollOffset;
	}

	inline uint8_t intensityFromCoords(int32_t x, int32_t y) {
		uint8_t edgeSize = getState().drums.edgeSize;
		uint8_t localX = (x % edgeSize);
		uint8_t localY = (y % edgeSize);
		uint8_t position = localX + (localY * edgeSize) + 1;

		// We use two bytes to increase accuracy and shift it down to one byte later
		uint32_t stepSize = 0xFFFF / (edgeSize * edgeSize);

		return (position * stepSize) >> 8;
	}

	RGB noteColours[kDisplayHeight * kDisplayWidth];
};

}; // namespace deluge::gui::ui::keyboard::layout
