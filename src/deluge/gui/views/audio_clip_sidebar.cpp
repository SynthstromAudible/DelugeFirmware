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

/// @file
/// @brief An audio clip's sidebar. See `audio_clip_sidebar.h`.

#include "gui/views/audio_clip_sidebar.h"

#include "gui/ui/ui.h"
#include "gui/views/view.h"
#include "hid/led/pad_leds.h"

#include <algorithm>

bool renderAudioClipSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	(void)whichRows;
	if (!image) {
		return true;
	}

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING) || isUIModeActive(UI_MODE_IMPLODE_ANIMATION)) {
		return true;
	}

	int32_t macroColumn = kDisplayWidth;
	bool armed = false;
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		RGB* const start = &image[y][kDisplayWidth];
		std::fill(start, start + kSideBarWidth, deluge::gui::colours::black);

		if (isUIModeActive(UI_MODE_HOLDING_SONG_BUTTON)) {
			armed |= view.renderMacros(macroColumn, y, -1, image, occupancyMask);
		}

		if (occupancyMask) {
			PadLEDs::refreshSidebarOccupancy(image[y], occupancyMask[y]);
		}
	}
	if (armed) {
		view.flashPlayEnable();
	}

	return true;
}
