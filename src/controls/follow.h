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

/// @file
/// @brief MIDI follow: which param a followed CC names on the clip the knobs point at, as an address.

#include "controls/knob_focus.h"
#include "deluge-project/param_address.h"
#include "plugin-api/types/plugin_id.h"

#include <cstdint>
#include <optional>
#include <span>

namespace deluge::controls {

/// @brief The id of the instrument param a kit's sound row is never followed on: its portamento, which a kit's rows do
/// not play.
inline constexpr uint32_t kFollowRefusedOnKitRow = 116;

/// @brief MIDI follow's CC assignments: which param each CC names on a sound and on a global-effectable owner.
struct FollowTables {
	std::span<const std::optional<project::ParamTarget>, 128> sound;  ///< a synth's or a kit row's param, by CC
	std::span<const std::optional<project::ParamTarget>, 128> global; ///< a kit's (affect-entire), audio track's or
	                                                                  ///< the song's param, by CC
};

/// @brief The param a followed CC names on @p focus (the selected or active clip's owner; a kit's selected row unless
/// affect-entire); nullopt where follow has none (portamento on a kit row, a MIDI or CV clip).
/// @param focus  What the followed clip's owner is: a sound for a synth or a kit's selected row, a chain for a kit with
///               affect-entire or an audio track, the song.
/// @param tables The CC assignments.
/// @param cc     The CC number, 0..127.
/// @return The param's address on the owner, in the focus's clip; none for an owner follow has no table for, a CC with
///         no assignment in the table the owner reads, or a param a kit's row is refused.
[[nodiscard]] std::optional<project::ParamAddress> resolve_follow(const KnobFocus& focus, const FollowTables& tables,
                                                                  uint8_t cc);

/// @brief The plugin whose descriptor rows the params follow names on @p focus have: a sound's, or the global
/// effectable's for a chain or the song.
[[nodiscard]] plugin::PluginId follow_plugin(const KnobFocus& focus);

} // namespace deluge::controls
