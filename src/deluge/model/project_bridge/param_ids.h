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

#include "engine-api/types/param.h"
#include "modulation/params/param.h"

#include <cstdint>

namespace deluge::bridge {

/// @brief The id the runtime's param at @p index of a @p kind collection has. Engine params go through their
/// engine's map (the Deluge instrument's for a sound's patched and unpatched params, the provisional
/// global-effectable table's for a global effectable's); a host space's param (MPE expression, a MIDI CC, a patch
/// cable) keeps its index as its id.
[[nodiscard]] deluge::engine::ParamId paramIdOf(deluge::modulation::params::Kind kind, int32_t index);

/// @brief The id of a @p kind collection's first param: the id its store's first lane has.
[[nodiscard]] deluge::engine::ParamId firstParamId(deluge::modulation::params::Kind kind);

} // namespace deluge::bridge
