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

#include "gui/context_menu/select_scale.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/display.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/song/song.h"

namespace deluge::gui::context_menu {
PLACE_SDRAM_BSS SelectScale selectScale{};

char const* SelectScale::getTitle() {
	using enum l10n::String;
	return l10n::get(STRING_FOR_SCALE);
}

std::span<char const*> SelectScale::getOptions() {
	return {names.data(), numOptions};
}

bool SelectScale::setupAndCheckAvailability() {
	numOptions = 0;
	Scale current = currentSong->getCurrentScale();
	currentOption = 0;
	for (int32_t s = 0; s <= NUM_PRESET_SCALES; s++) {
		bool isUser = s == USER_SCALE;
		if (isUser ? !currentSong->hasUserScale() : currentSong->disabledPresetScales[s]) {
			// The current scale is listed even if disabled in the menu, so that it can be seen and kept
			if (static_cast<Scale>(s) != current) {
				continue;
			}
		}
		if (static_cast<Scale>(s) == current) {
			currentOption = numOptions;
		}
		scales[numOptions] = static_cast<Scale>(s);
		names[numOptions] = getScaleName(static_cast<Scale>(s));
		numOptions++;
	}
	scrollPos = currentOption;
	return numOptions > 0;
}

bool SelectScale::acceptCurrentOption() {
	Scale scale = scales[currentOption];

	// Both views are InstrumentClipMinders, which show the new scale's name (or that it can't be used). The one
	// underneath then redraws with the new colours.
	bool inKeyboard = getRootUI() == &keyboardScreen;
	InstrumentClipMinder* minder = inKeyboard ? static_cast<InstrumentClipMinder*>(&keyboardScreen)
	                                          : static_cast<InstrumentClipMinder*>(&instrumentClipView);

	if (scale != currentSong->getCurrentScale() && minder->setScale(scale)) {
		if (inKeyboard) {
			keyboardScreen.onScaleChanged();
		}
		else {
			instrumentClipView.onScaleChanged();
		}
	}

	return false; // Closes the menu
}
} // namespace deluge::gui::context_menu
