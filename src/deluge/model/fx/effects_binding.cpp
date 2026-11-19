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

#include "model/fx/effects_binding.h"
#include "libdeluge/system.h" // AudioExcludedSection
#include "model/song/song.h"
#include "modulation/sidechain/sidechain.h"
#include "storage/flash_storage.h"
#include "util/structural_edit_probe.h"

namespace deluge::model {

deluge::effects::SongDefaults songDefaults() {
	// The song being loaded, or else the current one; with none, the stored default magnitude
	const Song* song = preLoadedSong != nullptr ? preLoadedSong : currentSong;
	const int32_t magnitude = song != nullptr
	                              ? song->project.transport.inside_world_tick_magnitude
	                                    + song->project.transport.inside_world_tick_magnitude_offset_from_bpm
	                              : FlashStorage::defaultMagnitude;
	return {.delay_sync_level = static_cast<SyncLevel>(8 - magnitude),
	        .sidechain = modulation::default_sidechain_state()};
}

EffectsBinding::EffectsBinding() {
	own_ = deluge::effects::with_song_defaults(own_, songDefaults());
	snapshot_.effects = own_; // nothing renders this owner yet
}

void EffectsBinding::store(const deluge::effects::EffectStates& next) {
	if (bound_ != nullptr) {
		*bound_ = next;
	}
	else {
		own_ = next;
	}
}

void EffectsBinding::bind(deluge::effects::EffectStates* record) {
	if (record != nullptr) {
		*record = effects();
	}
	else if (bound_ != nullptr) {
		own_ = *bound_;
	}
	bound_ = record;
}

void EffectsBinding::project(deluge::effects::ChainCore& core, std::size_t delayReserve,
                             std::optional<ModFXParam> modFXParam) {
	const ModFXType type = effects().mod_fx.type;
	deluge::effects::RingChange ring = core.mod_fx().prepare(type);
	deluge::effects::GranularBlockChange grain = core.granular().prepare(type);
	const deluge::effects::DelayType delayType = effects().delay.type;
	deluge::effects::DelayChange delayChange = core.delay().prepare(delayType);
	{
		AudioExcludedSection section; // the render reads the copy, the ring, the grain's blocks and the delay's arena
		snapshot_.effects = effects();
		if (modFXParam) {
			snapshot_.mod_fx_param = *modFXParam;
		}
		core.mod_fx().commit(snapshot_.effects.mod_fx.type, ring);
		noteStructuralEdit(StructuralContainer::MOD_FX_RING);
		core.granular().commit(snapshot_.effects.mod_fx.type, grain);
		noteStructuralEdit(StructuralContainer::GRAIN_BLOCK);
		core.delay().commit(delayType, delayChange, delayReserve);
		noteStructuralEdit(StructuralContainer::DELAY_TAPE);
	}
	core.delay().finish(delayChange);
	core.granular().finish(grain);
	core.mod_fx().finish(ring);
}

} // namespace deluge::model
