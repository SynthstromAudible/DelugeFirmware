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

#include "deluge-project/doc_drum.h"
#include "util/containers.h" // deluge::fast_map
#include <cstdint>

namespace deluge::project {

/// Per-drum structural records (M3 owner-3), owned by the persistent `project` tree and keyed by the drum's stable
/// `Drum::id`. The drum-structure analogue of `ClipStructure`: because it is keyed by the serial id — not by a
/// `Drum*` or list position — it is unaffected by drum reorders. A component of `deluge::project::Song` (a single
/// instance keyed by serial id; unlike clips there is no session/serial twin, since every drum gets its id via
/// `assignMissingIds`).
struct DrumStructure {
	deluge::fast_map<uint32_t, DocDrum> by_id;

	/// The drum's DocDrum, or nullptr if this drum has no tree-resident record.
	DocDrum* find(uint32_t id) {
		auto it = by_id.find(id);
		return it == by_id.end() ? nullptr : &it->second;
	}

	/// The drum's DocDrum, creating an empty one if absent.
	DocDrum& getOrCreate(uint32_t id) { return by_id[id]; }

	/// Drop the drum's record (on drum destruction).
	void remove(uint32_t id) { by_id.erase(id); }
};

} // namespace deluge::project
