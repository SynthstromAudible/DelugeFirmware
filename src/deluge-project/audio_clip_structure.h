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

#pragma once

#include "deluge-project/doc_audio_clip.h"
#include "util/containers.h" // deluge::fast_map
#include <cstdint>

namespace deluge::project {

/// Per-AudioClip descriptor records (M3 owner-4), owned by the persistent `project` tree and keyed by the clip's
/// stable `Clip::id`. The audio-descriptor analogue of `ClipStructure`, held SEPARATELY from it (DocClip is shared
/// with InstrumentClip; DocAudioClip is AudioClip-only). Because it is keyed by the serial id — not a `Clip*` or
/// array index — it is unaffected by clip reorders.
struct AudioClipStructure {
	deluge::fast_map<uint32_t, DocAudioClip> by_id;

	/// The clip's DocAudioClip, or nullptr if this clip has no tree-resident record.
	DocAudioClip* find(uint32_t id) {
		auto it = by_id.find(id);
		return it == by_id.end() ? nullptr : &it->second;
	}

	/// The clip's DocAudioClip, creating an empty one if absent.
	DocAudioClip& getOrCreate(uint32_t id) { return by_id[id]; }

	/// Drop the clip's record (on clip destruction).
	void remove(uint32_t id) { by_id.erase(id); }
};

} // namespace deluge::project
