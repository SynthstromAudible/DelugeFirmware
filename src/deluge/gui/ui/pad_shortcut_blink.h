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

#pragma once

#include "definitions_cxx.hpp"

#include <cstdint>

/// @brief The shortcut-pad blink: one parameter pad, flashed every `paramFrequency`-masked step, and the two
///        patching-source columns, each flashed on its own mask. The automation view and the legacy editor drive the
///        same one, and `TimerName::SHORTCUT_BLINK` ticks it.
class PadShortcutBlink {
public:
	PadShortcutBlink() { resetSources(); }

	/// @brief Blink pad (@p x, @p y) in @p colour every step whose counter has none of @p frequency's bits.
	void setup(int32_t x, int32_t y, int32_t frequency, int32_t colour = 0);
	/// @brief Blink only pad (@p x, @p y), every other step, starting now.
	void setupExclusive(int32_t x, int32_t y);
	/// @brief One step: the parameter pad on odd counts, the source pads on even ones; arms the next.
	void blink();
	/// @brief No source pad blinks.
	void resetSources();

	uint8_t paramX = kNoSelection;
	uint8_t paramY = 0;
	uint8_t paramColour = 0;
	uint8_t paramFrequency = 0;
	uint32_t counter = 0;
	uint8_t sourceFrequencies[2][kDisplayHeight]{};
	uint8_t sourceColours[2][kDisplayHeight]{};
	bool sourceAlternates[2][kDisplayHeight]{};
};

extern PadShortcutBlink padShortcutBlink;
