/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "deluge-project/param_automation.h"
#include "model/global_effectable/global_effectable_for_song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_node_vector.h"
#include <array>
#include <cstdint>

namespace deluge::project {

/// The song-global effect chain (master filters, FX, stutter, compressor) plus the unpatched-param
/// override store used while editing automation. A component of deluge::project::Song.
struct GlobalEffects {
	GlobalEffectableForSong global_effectable;
	/// Snapshot of the global unpatched param values, captured while a param is held in automation
	/// mode so the live (overridden) value can be restored afterwards.
	std::array<int32_t, deluge::modulation::params::kMaxNumUnpatchedParams> unautomated_param_values{};

	/// The authoritative document store of the song-global params/automation (param/automation DOM
	/// Phase 5c). Populated by capturing the live ParamManager after load; the destination for the
	/// authoritative flip (live ParamManager projected from this). Currently captured-only — the edit
	/// rerouting + save-reads-DOM that consume it land in later 5c sub-phases.
	DocParamManager params;

	/// Owner-held authoritative node storage for the song-global unpatched params (Phase 5c R2). Fixed-size
	/// so it never reallocates — the live unpatched AutoParams are redirected to view these entries
	/// (`AutoParam::redirectNodeStorage`), so automation editing operates directly on DOM-owned storage.
	/// Indexed by unpatched param id.
	std::array<ParamNodeVector, deluge::modulation::params::kMaxNumUnpatchedParams> param_node_store;
};

} // namespace deluge::project
