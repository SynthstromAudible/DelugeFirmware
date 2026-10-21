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

/// @file
/// @brief The arp preset's `get`/`set` hooks and the subtree's exported root span. The `Node` literals are in
///        `sound_arp.h`; every other arp node edits its row through a target.

#include "scenes/menu/nodes/sound_arp.h"

#include "scenes/bind/dispatch.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/context.h"

#include <optional>

namespace deluge::scenes::menu::nodes {

// The preset reads the engine's preset, where a chosen Custom lives (the record derives "up" from the fields it
// stands for), and writes the preset row through the menu's sink, which a kit row's affect-entire fans out.
int32_t get_arp_preset(const MenuContext& ctx) {
	const std::optional<project::SettingAddress> address =
	    ctx.facts != nullptr ? bind::setting_address(arp_row("arpeggiator/preset"), ctx.addresses) : std::nullopt;
	return address ? static_cast<int32_t>(ctx.facts->arp_preset(*address)) : 0;
}
void set_arp_preset(const MenuContext& ctx, int32_t value) {
	if (ctx.sink == nullptr || ctx.model == nullptr || ctx.facts == nullptr) {
		return;
	}
	if (const std::optional<bind::WriteRequest> request =
	        bind::request_for(arp_row("arpeggiator/preset"), *ctx.model, ctx.addresses, *ctx.facts, value)) {
		ctx.sink->write(*request);
	}
}

namespace {
constexpr const Node* kRoots[] = {&arp};
} // namespace

const std::span<const Node* const> kSoundArpChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
