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

#include "deluge-project/containers.h"
#include "deluge-project/doc_audio_clip.h"
#include <cstdint>

namespace deluge::project {

/// @brief Per-AudioClip descriptor records, owned by the persistent `project` tree and keyed by the clip's stable
/// `Clip::id`.
///
/// The audio-descriptor analogue of `ClipStructure`, held separately from it (DocClip is shared with InstrumentClip;
/// DocAudioClip is AudioClip-only). Because it is keyed by the serial id, not a `Clip*` or array index, it is
/// unaffected by clip reorders.
struct AudioClipStructure {
	model_map<uint32_t, DocAudioClip> by_id;

	/// @brief Find the clip's record.
	/// @param id The clip's stable `Clip::id`.
	/// @return The clip's DocAudioClip, or nullptr if this clip has no tree-resident record.
	DocAudioClip* find(uint32_t id) {
		auto it = by_id.find(id);
		return it == by_id.end() ? nullptr : &it->second;
	}
	/// @copydoc find
	const DocAudioClip* find(uint32_t id) const {
		auto it = by_id.find(id);
		return it == by_id.end() ? nullptr : &it->second;
	}

	/// @brief Get the clip's record, creating an empty one if absent.
	/// @param id The clip's stable `Clip::id`.
	/// @return The clip's DocAudioClip.
	DocAudioClip& getOrCreate(uint32_t id) { return by_id[id]; }

	/// @brief Drop the clip's record (on clip destruction).
	/// @param id The clip's stable `Clip::id`.
	void remove(uint32_t id) { by_id.erase(id); }
};

} // namespace deluge::project
