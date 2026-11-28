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

#include "instruments/classic/params.h"
#include "project/param_address.h"
#include "synth/modulation/cable.h"

#include <cstdint>
#include <optional>

namespace deluge::modulation {

/// @brief What a knob controls: a param of its owner, by its id, or a sound's cable depth.
using KnobTarget = std::optional<deluge::project::ParamTarget>;

/// @brief The sound param or global-effectable param at the runtime's flat index @p flat, as a knob target: a
/// patched param's runtime index, an unpatched one's 90 on from its runtime index (in a sound and a global effectable
/// alike), named by its id (@c instruments::classic::flat_param_id).
/// @param flat The param's flat index.
/// @return The knob target.
[[nodiscard]] constexpr deluge::project::ParamTarget knobParam(uint32_t flat) {
	return deluge::project::engine_target(deluge::instruments::classic::flat_param_id(flat).value);
}

/// @brief @p target in the runtime's descriptor layout: a param as its flat index with no source, a cable depth as its
/// packed word; no target, or an arp param (which the layout does not name), as the null word.
[[nodiscard]] deluge::synth::CableTarget knobWord(const KnobTarget& target);

/// @brief The knob target of the descriptor word @p word; the inverse of knobWord.
[[nodiscard]] KnobTarget knobTargetOf(deluge::synth::CableTarget word);

} // namespace deluge::modulation
