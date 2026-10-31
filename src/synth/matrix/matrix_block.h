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

#pragma once

#include "modulation/params/param.h" // params::kNumParams
#include "synth/matrix/program.h"
#include "synth/modulation/cable.h"

#include <array>
#include <cstdint>

namespace deluge::synth::matrix {

/// @brief What one patching call reads beside its program: each param's preset and each routed cable's depth, gathered
/// by the instrument as copies, never views into the live stores.
struct MatrixBlock {
	const MatrixProgram* program{}; ///< the instrument's compiled cables; null when it has none
	/// Each param's preset, folded with automation and expression (the smoothed one for a param being smoothed), Q31 as
	/// stored. Only the gathered entries are filled: the routed destinations' for a block, every param's otherwise.
	/// Left uninitialised, as the depths are: a gather runs twice a block, and the patching reads back only what it
	/// filled.
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
	std::array<int32_t, modulation::params::kNumParams> preset_values;
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
	std::array<int32_t, kMaxCables> depths; ///< each routed cable's depth, in routing order: [-2^30, 2^30]

	[[nodiscard]] int32_t preset_value(int32_t p) const { return preset_values[p]; }
};

} // namespace deluge::synth::matrix
