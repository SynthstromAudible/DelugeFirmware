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

#include "gui/ui/rename/rename_output_ui.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/views/arranger_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/output.h"
#include "model/song/song.h"

PLACE_SDRAM_BSS RenameOutputUI renameOutputUI{"Track name"};

String RenameOutputUI::getName() const {
	return output->name;
}

bool RenameOutputUI::trySetName(String* name) {
	// Duplicate names not allowed for audio outputs.
	if (!output->name.equalsCaseIrrespective(name) && currentSong->getAudioOutputFromName(name)) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_DUPLICATE_NAMES));
		return false;
	}
	output->name.set(name);
	return true;
}
