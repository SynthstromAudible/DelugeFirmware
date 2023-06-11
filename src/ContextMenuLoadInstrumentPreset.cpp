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

#include <ContextMenuLoadInstrumentPreset.h>
#include "LoadInstrumentPresetUI.h"
#include "numericdriver.h"

ContextMenuLoadInstrumentPreset contextMenuLoadInstrumentPreset;

ContextMenuLoadInstrumentPreset::ContextMenuLoadInstrumentPreset() {
#if HAVE_OLED
	title = "Load preset";
#endif
}

char const** ContextMenuLoadInstrumentPreset::getOptions() {
	static char const* options[] = {"Clone"}; // "REFRESH",
	return options;
}

bool ContextMenuLoadInstrumentPreset::acceptCurrentOption() {
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
