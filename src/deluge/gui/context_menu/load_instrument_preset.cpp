/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "gui/context_menu/load_instrument_preset.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "hid/display/display.h"

namespace deluge::gui::context_menu {
PLACE_SDRAM_BSS LoadInstrumentPreset loadInstrumentPreset{};

char const* LoadInstrumentPreset::getTitle() {
	using enum l10n::String;
	return l10n::get(STRING_FOR_LOAD_PRESET);
}

std::span<char const*> LoadInstrumentPreset::getOptions() {
	using enum l10n::String;
	static char const* options[] = {l10n::get(STRING_FOR_CLONE)};
	return {options, 1};
}

bool LoadInstrumentPreset::acceptCurrentOption() {
	Error error;

	switch (currentOption) {
	/*
	case 0: // Refresh
		return true;
		*/
	default: // Clone
		error = loadInstrumentPresetUI.performLoad(true);
		if (error != Error::NONE) {
			display->displayError(error);
			return true;
		}
		loadInstrumentPresetUI.close();
		return true;
	}
}
} // namespace deluge::gui::context_menu
