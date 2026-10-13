/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "model/global_effectable/global_effectable_for_song.h"
#include "model/mod_controllable/delay_arena_app.h"
#include "model/mod_controllable/mod_fx_allocator.h"

GlobalEffectableForSong::GlobalEffectableForSong()
    : chain_{deluge::model::kModFxRingAllocator, deluge::model::kGranularAllocator, deluge::model::delayArena()} {
	// A new owner's settings live in its chain state, and its filter mode and mod FX type are set, once its chain
	// exists.
	bindChainState();
	setLpfMode(FilterMode::TRANSISTOR_24DB);
	filter().reset();
	assignModFXType(ModFXType::NONE);
	modKnobMode = 1;
	// UI for kit compressors is TBD so they can only be accessed in song
	maxCompParam = util::to_underlying(CompParam::LAST);
}

GlobalEffectableForSong::~GlobalEffectableForSong() {
	releaseChainMemory();
}
