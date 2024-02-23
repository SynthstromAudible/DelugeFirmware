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
#include "gui/ui/keyboard/notes_state.h"
#include "gui/ui/keyboard/layout/column_controls.h"

namespace deluge::gui::ui::keyboard::layout {
class Snake {
	public:
		int32_t x = 0;
		int32_t y = 0;
		bool dead;
	    int getDirection() {
	        return direction;
	    }
	    void setDirection(int d) {
	        direction = d % 4;
	    }
	    void turnClockwise() {
	        direction = (direction + 1) % 4;
	    }
		void turnCounterclockwise() {
			if (direction > 0) {
				direction = (direction - 1) % 4;
			} else { // if 0 (up) then set to 3 (left)
				direction = 3;
			}
		}
		void stepForward() {
	        switch(direction) {
				// I tried using std::clamp() here but vscode said I couldn't?
				// I just wanted to do clamp(y,0,kDisplayHeight) and so on
				case 0: // UP
					y = (y + 1) % kDisplayHeight;
					break;
				case 1: // RIGHT
					x = (x + 1) % kDisplayWidth;
					break;
				case 2: // DOWN
					if (y > 0) {
						y = (y - 1) % kDisplayHeight;
					} else {
						y = kDisplayHeight - 1;
					}
					break;
				case 3: // LEFT
					if (x > 0) {
						x = (x - 1) % kDisplayWidth;
					} else {
						x = kDisplayWidth - 1;
					}
					break;
				default:
					break;
			}
	    }

	private:
		int32_t direction = 0; // 0 up, 1 right, 2 down, 3 left
};

class KeyboardLayoutSnake : public ColumnControlsKeyboard {
public:
	KeyboardLayoutSnake() = default;
	~KeyboardLayoutSnake() override = default;

	void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) override;
	void handleVerticalEncoder(int32_t offset) override;
	void handleHorizontalEncoder(int32_t offset, bool shiftEnabled) override;
	void precalculate() override;
	void renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) override;

	char const* name() override { return "Snake"; }
	bool supportsInstrument() override { return true; }
	bool supportsKit() override { return false; }

	bool snakeFood[kDisplayWidth][kDisplayHeight];
	Snake snakeGreen;

private:
	inline uint8_t noteFromCoords(int32_t x, int32_t y) { return x + y * kDisplayWidth; }

};

}; // namespace deluge::gui::ui::keyboard::layout
