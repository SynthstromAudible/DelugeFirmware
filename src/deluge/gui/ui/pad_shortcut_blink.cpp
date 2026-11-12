/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "gui/ui/pad_shortcut_blink.h"

#include "gui/ui_timer_manager.h"
#include "hid/led/pad_leds.h"

#include <cstring>

PadShortcutBlink padShortcutBlink{};

void PadShortcutBlink::setup(int32_t x, int32_t y, int32_t frequency, int32_t colour) {
	paramX = x;
	paramY = y;
	paramColour = colour;
	counter = 0;
	paramFrequency = frequency;
}

void PadShortcutBlink::setupExclusive(int32_t x, int32_t y) {
	resetSources();
	setup(x, y, 1);
	blink();
}

void PadShortcutBlink::blink() {
	// Params and sources blink at slightly different times: two pads on one row cannot flash together.
	uint32_t counterForNow = counter >> 1;

	if (counter & 1) {
		if ((counterForNow & paramFrequency) == 0) {
			PadLEDs::flashMainPad(paramX, paramY, paramColour);
		}
		uiTimerManager.setTimer(TimerName::SHORTCUT_BLINK, 180);
	}
	else {
		for (int32_t x = 0; x < 2; x++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				if (sourceFrequencies[x][y] != 255 && (counterForNow & sourceFrequencies[x][y]) == 0) {
					auto colour = sourceColours[x][y];
					// The frequency is a mask of low counter bits. The next bit alternates on successive flashes,
					// including when the source blinks slowly.
					if (sourceAlternates[x][y] && (counterForNow & (sourceFrequencies[x][y] + 1)) != 0) {
						colour = 0b00000011; // second layer
					}
					PadLEDs::flashMainPad(x + 14, y, colour);
				}
			}
		}
		uiTimerManager.setTimer(TimerName::SHORTCUT_BLINK, 20);
	}

	counter++;
}

void PadShortcutBlink::resetSources() {
	std::memset(sourceFrequencies, 255, sizeof(sourceFrequencies));
	std::memset(sourceColours, 0, sizeof(sourceColours));
	std::memset(sourceAlternates, 0, sizeof(sourceAlternates));
}
