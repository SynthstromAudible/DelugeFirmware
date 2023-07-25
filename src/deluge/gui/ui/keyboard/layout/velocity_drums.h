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

namespace keyboard::layout {

constexpr int kMinDrumPadEdgeSize = 1;
constexpr int kMaxDrumPadEdgeSize = 8;

class KeyboardLayoutVelocityDrums : KeyboardLayout {
public:
	KeyboardLayoutVelocityDrums() {}
	virtual ~KeyboardLayoutVelocityDrums() {}

	virtual void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]);
	virtual void handleVerticalEncoder(int offset);
	virtual void handleHorizontalEncoder(int offset, bool shiftEnabled);
	virtual void precalculate();

	virtual void renderPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]);

	virtual char* name() { return "Drums"; }
	virtual bool supportsInstrument() { return false; }
	virtual bool supportsKit() { return true; }

private:
	inline uint8_t noteFromCoords(int x, int y) {
		uint8_t edgeSize = (uint32_t)getState()->drums.edgeSize;
		uint8_t padsPerRow = kDisplayWidth / edgeSize;
		return (x / edgeSize) + ((y / edgeSize) * padsPerRow) + getState()->drums.scrollOffset;
	}

	inline uint8_t intensityFromCoords(int x, int y) {
		uint8_t edgeSize = getState()->drums.edgeSize;
		uint8_t localX = (x % edgeSize);
		uint8_t localY = (y % edgeSize);
		uint8_t position = localX + (localY * edgeSize) + 1;

		// We use 0xFFFF to increase accuracy and shift it down later
		uint32_t stepSize = 0xFFFF / (edgeSize * edgeSize);

		return (position * stepSize) >> 8;
	}

	uint8_t noteColours[kDisplayHeight * kMaxDrumPadEdgeSize + kDisplayWidth][3];
};

}; // namespace keyboard::layout
