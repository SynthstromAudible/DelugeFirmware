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

#include "model/global_effectable/global_effectable_for_song.h"
#include "modulation/params/param.h"
#include <array>
#include <cstdint>

namespace deluge::project {

/// The song-global effect chain (master filters, FX, stutter, compressor) plus the unpatched-param
/// override snapshot used while editing automation. A component of deluge::project::Song.
struct GlobalEffects {
	GlobalEffectableForSong global_effectable;
	/// Snapshot of the global unpatched param values, captured while a param is held in automation
	/// mode so the live (overridden) value can be restored afterwards.
	std::array<int32_t, deluge::modulation::params::kMaxNumUnpatchedParams> unautomated_param_values{};
};

} // namespace deluge::project
