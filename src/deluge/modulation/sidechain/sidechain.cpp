/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "modulation/sidechain/sidechain.h"
#include "definitions_cxx.hpp"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/sync.h"
#include "storage/flash_storage.h"
#include "util/functions.h"

namespace deluge::modulation {

effects::SidechainState default_sidechain_state() {
	effects::SidechainState settings{
	    .attack = getParamFromUserValue(util::to_underlying(params::STATIC_SIDECHAIN_ATTACK), 7),
	    .release = getParamFromUserValue(util::to_underlying(params::STATIC_SIDECHAIN_RELEASE), 28),
	    .sync_type = SYNC_TYPE_EVEN,
	};
	// The default sync level follows the song being loaded, or else the current one: the default synth with no card,
	// and presets saved without a sync level, take it when they are read.
	const Song* song = preLoadedSong != nullptr ? preLoadedSong : currentSong;
	if (song != nullptr) {
		settings.sync_level =
		    static_cast<SyncLevel>(7
		                           - (song->project.transport.inside_world_tick_magnitude
		                              + song->project.transport.inside_world_tick_magnitude_offset_from_bpm));
	}
	else {
		settings.sync_level = static_cast<SyncLevel>(7 - FlashStorage::defaultMagnitude);
	}
	return settings;
}

} // namespace deluge::modulation
