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
/// @brief How an owner publishes its remote-controls pages to the host: one record per plugin id, as function
///        pointers, the way a plugin publishes its param naming.

#include "plugin-api/state_handle.h"
#include "plugin-api/types/plugin_id.h"
#include "plugin-api/types/remote_controls.h"

#include <cstdint>
#include <span>

namespace deluge::plugin {

/// @brief What a publisher may read when it builds a page: the owner's own state and the host's few facts.
struct PageContext {
	const StateHandle* state = nullptr; ///< the owner's state (the publisher knows its type); null for none
	bool song = false;                  ///< the owner is the song's chain
	bool alt_delay = false;             ///< the device's alternative delay-knob setting
};

/// @brief One plugin's remote-controls pages, as function pointers.
struct RemoteControls {
	PluginId plugin;
	/// @brief The number of device pages.
	uint32_t (*count)(const PageContext& ctx) = nullptr;
	/// @brief Device page @p index into @p out. @return false past the end.
	bool (*get)(const PageContext& ctx, uint32_t index, RemotePage& out) = nullptr;
	/// @brief The page (@p section, @p page_id) into @p out. Which pages it serves is the owner's choice and need not
	/// be
	///        the device pages get lists (the instrument's serves its filter cycle on any section). @return false for
	///        none.
	bool (*find)(const PageContext& ctx, uint32_t section, uint32_t page_id, RemotePage& out) = nullptr;
	/// @brief A preset page's slot for @p param: its kind and press.
	RemoteSlot (*decorate)(const PageContext& ctx, ParamId param) = nullptr;
};

/// @brief The record for @p plugin in @p records, or nullptr.
/// @param records The available records.
/// @param plugin  The plugin to look up.
/// @return The matching record, or nullptr.
[[nodiscard]] constexpr const RemoteControls* remote_controls_for(std::span<const RemoteControls> records,
                                                                  PluginId plugin) {
	for (const RemoteControls& r : records) {
		if (r.plugin == plugin) {
			return &r;
		}
	}
	return nullptr;
}

} // namespace deluge::plugin
