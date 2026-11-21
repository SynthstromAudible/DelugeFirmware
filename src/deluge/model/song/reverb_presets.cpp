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

#include "model/song/reverb_presets.h"

#include "deluge-project/song.h"
#include "effects/reverb_presets.h"
#include "model/edits/setting_edit.h"
#include "model/song/song.h"

namespace deluge::model {

size_t reverbPresetOf(const project::Song& song) {
	return effects::nearest_reverb_preset_of(song.reverb.room_size, song.reverb.damping);
}

void cycleReverbPreset(::Song& song) {
	const effects::ReverbPreset& next =
	    effects::kReverbPresets[(reverbPresetOf(song.project) + 1) % effects::kReverbPresets.size()];
	edits::songSettingFromMenuIn(song, "reverb/roomSize", next.room_size);
	edits::songSettingFromMenuIn(song, "reverb/dampening", next.damping);
}

} // namespace deluge::model
