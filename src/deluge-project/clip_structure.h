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

#include "deluge-project/doc_clip.h"
#include "util/containers.h" // deluge::fast_map
#include <cstdint>

namespace deluge::project {

/// Per-clip structural records (M3 owner-1), owned by the persistent `project` tree and keyed by the clip's stable
/// `Clip::id`. The clip-structure analogue of `ClipParams`: because it is keyed by the serial id — not by a
/// `Clip*` or array index — it is unaffected by clip reorders. A component of `deluge::project::Song` (one instance
/// keyed by serial id, a transient twin keyed by session id for not-yet-saved clips).
struct ClipStructure {
	deluge::fast_map<uint32_t, DocClip> by_id;

	/// The clip's DocClip, or nullptr if this clip has no tree-resident record.
	DocClip* find(uint32_t id) {
		auto it = by_id.find(id);
		return it == by_id.end() ? nullptr : &it->second;
	}

	/// The clip's DocClip, creating an empty one if absent.
	DocClip& getOrCreate(uint32_t id) { return by_id[id]; }

	/// Drop the clip's record (on clip destruction).
	void remove(uint32_t id) { by_id.erase(id); }
};

} // namespace deluge::project
