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
	currentOption = 0;
	Scale current = currentSong->getCurrentScale();

	// The current scale is listed even if it isn't otherwise available, so that it can be seen and kept
	auto addOption = [&](Scale scale, bool available) {
		if (scale == current) {
			currentOption = numOptions;
		}
		else if (!available) {
			return;
		}
		scales[numOptions] = scale;
		names[numOptions] = getScaleName(scale);
		numOptions++;
	};

	for (int32_t preset = 0; preset < NUM_PRESET_SCALES; preset++) {
		addOption(static_cast<Scale>(preset), !currentSong->disabledPresetScales[preset]);
	}
	addOption(USER_SCALE, currentSong->hasUserScale());

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
