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

#include "ui/controls/follow.h"

namespace deluge::ui::controls {

std::optional<project::ParamAddress> resolve_follow(const KnobFocus& focus, const FollowTables& tables, uint8_t cc) {
	if (cc >= tables.sound.size()) {
		return std::nullopt;
	}
	const std::optional<project::ParamAddress> owner = knob_owner(focus);
	if (!owner) {
		return std::nullopt;
	}
	if (const auto* sound = std::get_if<SoundFocus>(&focus.owner)) {
		const std::optional<project::ParamTarget>& target = tables.sound[cc];
		if (!target) {
			return std::nullopt;
		}
		if (sound->slot.kind == project::SlotAddress::Kind::DRUM && target->space == project::ParamSpace::ENGINE
		    && target->id.value == kFollowRefusedOnKitRow) {
			return std::nullopt;
		}
		return project::ParamAddress::of(owner->owner, *target, owner->clip_id);
	}
	if (std::holds_alternative<SongFocus>(focus.owner) || std::holds_alternative<ChainFocus>(focus.owner)) {
		const std::optional<project::ParamTarget>& target = tables.global[cc];
		return target ? std::optional{project::ParamAddress::of(owner->owner, *target, owner->clip_id)} : std::nullopt;
	}
	// A MIDI or CV track's CCs are its own, not params follow moves.
	return std::nullopt;
}

plugin::PluginId follow_plugin(const KnobFocus& focus) {
	return std::holds_alternative<SoundFocus>(focus.owner) ? plugin::kDelugeInstrument : plugin::kGlobalEffectable;
}

} // namespace deluge::ui::controls
