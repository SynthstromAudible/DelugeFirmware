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
#include "gui/l10n/l10n.h"
#include "model/edits/setting_edit.h"
#include "model/song/song.h"

#include <cstdlib>

namespace deluge::model {

namespace {

/// Each preset's name.
constexpr std::array<l10n::String, kNumReverbPresets> kReverbPresetNames{l10n::String::STRING_FOR_SMALL_ROOM_REVERB,
                                                                         l10n::String::STRING_FOR_MEDIUM_ROOM_REVERB,
                                                                         l10n::String::STRING_FOR_LARGE_ROOM_REVERB};

} // namespace

size_t reverbPresetOf(const project::Song& song) {
	const auto room = static_cast<int32_t>(song.reverb.room_size * 50);
	const auto damping = static_cast<int32_t>(song.reverb.damping * 50);
	size_t nearest = 0;
	int32_t lowest = 1000;
	for (size_t p = 0; p < kNumReverbPresets; p++) {
		const int32_t distance =
		    std::abs(room - kReverbPresetRoomSize[p]) + std::abs(damping - kReverbPresetDamping[p]);
		if (distance < lowest) {
			lowest = distance;
			nearest = p;
		}
	}
	return nearest;
}

const char* reverbPresetName(size_t preset) {
	return l10n::get(kReverbPresetNames[preset % kNumReverbPresets]);
}

void cycleReverbPreset(::Song& song) {
	const size_t next = (reverbPresetOf(song.project) + 1) % kNumReverbPresets;
	edits::songSettingFromMenuIn(song, "reverb/roomSize", kReverbPresetRoomSize[next]);
	edits::songSettingFromMenuIn(song, "reverb/dampening", kReverbPresetDamping[next]);
}

} // namespace deluge::model
