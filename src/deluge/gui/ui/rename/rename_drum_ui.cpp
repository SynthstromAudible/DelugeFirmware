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

#include "gui/ui/rename/rename_drum_ui.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

PLACE_SDRAM_BSS RenameDrumUI renameDrumUI{"Drum Name"};

String RenameDrumUI::getName() const {
	return getDrum()->name;
}

SoundDrum* RenameDrumUI::getDrum() const {
	return (SoundDrum*)soundEditor.currentSound;
}

bool RenameDrumUI::trySetName(String* name) {
	// don't let user set a name that is a duplicate of another name that has been set for another drum
	if (!getDrum()->name.equalsCaseIrrespective(name) && getCurrentKit()->getDrumFromName(name->get())) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_DUPLICATE_NAMES));
		return false;
	}
	getDrum()->name.set(name);
	return true;
}
