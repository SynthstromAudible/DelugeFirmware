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

#include "deluge-project/param_address.h"
#include "synth/modulation/cable.h"

#include <cstdint>
#include <optional>

namespace deluge::modulation {

/// @brief What a knob controls: a param of its owner, by the owner's flat id (an unpatched param's id is 90 on from
/// its runtime index, in a sound and a global effectable alike), or a sound's cable depth.
using KnobTarget = std::optional<deluge::project::ParamTarget>;

/// @brief The sound param or global-effectable param whose flat id is @p flat, as a knob target.
[[nodiscard]] constexpr deluge::project::ParamTarget knobParam(uint32_t flat) {
	return deluge::project::engine_target(flat);
}

/// @brief @p target in the runtime's descriptor layout: no target is null, a param the param with no source, a cable
/// depth its packed word.
[[nodiscard]] deluge::synth::CableTarget knobWord(const KnobTarget& target);

/// @brief The knob target of the descriptor word @p word; the inverse of knobWord.
[[nodiscard]] KnobTarget knobTargetOf(deluge::synth::CableTarget word);

} // namespace deluge::modulation
