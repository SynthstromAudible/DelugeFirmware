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

#include "plugin/types/param_kind.h"
#include "plugin/types/plugin_id.h"
#include "project/owner_plugin.h"
#include "project/param_address.h"
#include "ui/controls/knob_focus.h"

#include <cstdint>
#include <optional>
#include <span>

namespace deluge::ui::controls {

/// @brief The id of the instrument param a kit's sound row is never followed on: its portamento, which a kit's rows do
/// not play.
inline constexpr uint32_t kFollowRefusedOnKitRow = 101;

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

/// @brief The param MIDI follow names for a knob's param, in the plugin the context follows.
///
/// A sound's context follows the patched, unpatched-sound and cable params, a global effectable's the unpatched-global
/// ones; a MIDI CC, an expression axis and an arp param follow in either. A cable's depth id is in no plugin's table;
/// every other engine id must be in its owner's.
///
/// @tparam InTable Callable `bool(plugin::PluginId, plugin::ParamId)`: whether the plugin's table has the param.
/// @param owner    Whose params the context follows.
/// @param kind     The kind of the param the knob turned.
/// @param id       The param's id: a MIDI CC number, an expression axis, or the owner plugin's id.
/// @param in_table The plugin table's membership test.
/// @return The followed target, or nullopt for a param of the other owner's plugin, a kind with no follow (none,
///         static, or not a kind), or an id its owner's table lacks.
// TODO(c++26): take std::function_ref<bool(plugin::PluginId, plugin::ParamId)> once the device's libstdc++ provides it
// (arm-none-eabi GCC 16; 15.2.1 does not).
template <class InTable>
[[nodiscard]] std::optional<project::ParamTarget> follow_target(project::OwnerPlugin owner, plugin::ParamKind kind,
                                                                plugin::ParamId id, const InTable& in_table) {
	switch (kind) {
	case plugin::ParamKind::MIDI:
		return project::ParamTarget{.space = project::ParamSpace::MIDI_CC, .id = id};
	case plugin::ParamKind::EXPRESSION:
		return project::ParamTarget{.space = project::ParamSpace::EXPRESSION, .id = id};
	case plugin::ParamKind::ARP:
		return project::ParamTarget{.space = project::ParamSpace::ARP, .id = id};
	case plugin::ParamKind::PATCH_CABLE:
		return owner == project::OwnerPlugin::INSTRUMENT ? std::optional{project::engine_target(id.value)}
		                                                 : std::nullopt;
	case plugin::ParamKind::PATCHED:
	case plugin::ParamKind::UNPATCHED_SOUND:
		return owner == project::OwnerPlugin::INSTRUMENT && in_table(plugin::kDelugeInstrument, id)
		           ? std::optional{project::engine_target(id.value)}
		           : std::nullopt;
	case plugin::ParamKind::UNPATCHED_GLOBAL:
		return owner == project::OwnerPlugin::GLOBAL_EFFECTABLE && in_table(plugin::kGlobalEffectable, id)
		           ? std::optional{project::engine_target(id.value)}
		           : std::nullopt;
	case plugin::ParamKind::NONE:
	case plugin::ParamKind::STATIC:
		break;
	}
	return std::nullopt; // a kind with no follow, or a value that is no kind
}

} // namespace deluge::ui::controls
