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

#include "deluge-project/param_automation.h" // DocParamManager
#include "util/containers.h"                 // deluge::fast_map
#include <cstdint>

namespace deluge::project {

/// Per-clip automation node storage, owned by the persistent `project` tree and keyed by the clip's stable
/// `Clip::serialId`. This is what makes a clip's params tree-resident and id-addressable (`Song::resolveParam`
/// with a `Clip` owner), instead of engine-owned on the live `ParamSet::domStore_`. Because it is keyed by the
/// serial id — not by a `Clip*` or array index — it is unaffected by clip reorders and by
/// `stealParamCollectionsFrom`/`ParamBackup` moving the engine cursor-holder (the nodes stay here, resolved by
/// id). A component of `deluge::project::Song`.
struct ClipParams {
	deluge::fast_map<uint32_t, DocParamManager> by_serial_id;

	/// The clip's DocParamManager, or nullptr if this clip has no tree-resident store.
	DocParamManager* find(uint32_t serialId) {
		auto it = by_serial_id.find(serialId);
		return it == by_serial_id.end() ? nullptr : &it->second;
	}

	/// The clip's DocParamManager, creating an empty one if absent.
	DocParamManager& getOrCreate(uint32_t serialId) { return by_serial_id[serialId]; }

	/// Drop the clip's store (on clip destruction).
	void remove(uint32_t serialId) { by_serial_id.erase(serialId); }
};

} // namespace deluge::project
