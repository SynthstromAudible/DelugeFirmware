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
#include "deluge-project/doc_output.h"
#include <cstdint>

namespace deluge::project {

/// @brief Per-output identity records, owned by the persistent `project` tree and keyed by the output's stable
/// `Output::id`.
///
/// The output-identity analogue of `DrumStructure`: because it is keyed by the serial id — not by an
/// `Output*` or list position — it is unaffected by output reorders. A component of `deluge::project::Song` (a single
/// instance keyed by serial id; every output gets its id via `assignMissingIds`).
struct OutputStructure {
	model_map<uint32_t, DocOutput> by_id;

	/// @brief The output's DocOutput, or nullptr if this output has no tree-resident record.
	/// @param id The output's stable id.
	/// @return The output's DocOutput, or nullptr if it has no tree-resident record.
	DocOutput* find(uint32_t id) {
		auto it = by_id.find(id);
		return it == by_id.end() ? nullptr : &it->second;
	}
	/// @copydoc find
	const DocOutput* find(uint32_t id) const {
		auto it = by_id.find(id);
		return it == by_id.end() ? nullptr : &it->second;
	}

	/// @brief The output's DocOutput, creating an empty one if absent.
	/// @param id The output's stable id.
	/// @return The output's DocOutput.
	DocOutput& getOrCreate(uint32_t id) { return by_id[id]; }

	/// @brief Drop the output's record (on output destruction).
	/// @param id The output's stable id.
	void remove(uint32_t id) { by_id.erase(id); }
};

} // namespace deluge::project
