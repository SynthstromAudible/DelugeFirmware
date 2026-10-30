/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "synth/modulation/cable.h"
#include "synth/modulation/patch_routing.h"

#include <array>
#include <cstdint>

/// @brief The per-block inputs the Patcher reads, gathered by Sound::patchControlBlock once per patching call as
/// copies, never as a view into the live stores: the clip's routing, each routed cable's depth, and preset values.
///
/// The per-block patching reads the presets of the routed destinations only, so its block gathers those; the initial
/// patching and the recalculation of a param with no cables read any param's, so theirs gathers every param.
struct ControlBlock {
	deluge::synth::PatchRouting routing;
	/// Each param's current preset value, already folded with automation and expression, the smoothed one for the
	/// param being smoothed (Q31 as stored). Only the entries the gather was asked for are filled.
	std::array<int32_t, deluge::modulation::params::kNumParams> preset_values;
	/// Each routed cable's current depth, in routing order: [-2^30, 2^30].
	std::array<int32_t, deluge::synth::kMaxCables> depths;

	[[nodiscard]] int32_t presetValue(int32_t p) const { return preset_values[p]; }
};
