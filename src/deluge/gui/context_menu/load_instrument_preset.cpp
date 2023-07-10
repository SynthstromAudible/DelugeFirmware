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
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "hid/display/numeric_driver.h"

namespace deluge::gui::context_menu {
LoadInstrumentPreset loadInstrumentPreset{};

char const* LoadInstrumentPreset::getTitle() {
	static char const* title = "Load preset";
	return title;
}

Sized<char const**> LoadInstrumentPreset::getOptions() {
	static char const* options[] = {"Clone"}; // "REFRESH",
	return {options, 1};
}

bool LoadInstrumentPreset::acceptCurrentOption() {
	int error;

	switch (currentOption) {
	/*
	case 0: // Refresh
		return true;
		*/
	default: // Clone
		error = loadInstrumentPresetUI.performLoad(true);
		if (error) {
			numericDriver.displayError(error);
			return true;
		}
		loadInstrumentPresetUI.close();
		return true;
	}
}
} // namespace deluge::gui::context_menu
