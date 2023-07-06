/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "arpeggiator.h"
#include "gui/ui/sound_editor.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "model/clip/instrument_clip.h"

namespace menu_item::submenu {

void Arpeggiator::beginSession(MenuItem* navigatedBackwardFrom) {

	soundEditor.currentArpSettings = soundEditor.editingKit()
	                                     ? &((SoundDrum*)soundEditor.currentSound)->arpSettings
	                                     : &((InstrumentClip*)currentSong->currentClip)->arpSettings;
	Submenu::beginSession(navigatedBackwardFrom);
}

} // namespace menu_item::submenu
