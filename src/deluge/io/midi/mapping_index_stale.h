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

// What tells the mapping index (io/midi/mapping_index.h) that the song or the devices changed under it. Marking
// neither allocates nor locks, so it is safe on any tier, the audio tier and a structural edit's AudioExcludedSection
// included; refreshMappingIndex() rebuilds on the app loop before the next incoming message is matched.

#include <atomic>

namespace deluge {

namespace mapping_index_detail {
/// Set when what the index was built from changed: the table, the song's outputs, kit rows or session clips, or
/// which song is current.
inline std::atomic<bool> mapping_index_stale{false};
/// Set when the devices a source can name changed: a device entry made or renamed, or a device plugged in or out.
inline std::atomic<bool> mapping_devices_changed{false};
} // namespace mapping_index_detail

/// @brief [task] [audio] Note that the current song's bindings, or what they reach, changed: the index is rebuilt
/// before the next incoming message is matched.
inline void markMappingIndexStale() noexcept {
	mapping_index_detail::mapping_index_stale.store(true, std::memory_order_release);
}

/// @brief [task] [audio] Note that the devices a binding's source can name changed: before the next incoming message
/// is matched, the bindings naming a hosted device are projected again and the index rebuilt.
inline void markMappingDevicesChanged() noexcept {
	mapping_index_detail::mapping_devices_changed.store(true, std::memory_order_release);
	markMappingIndexStale();
}

/// @brief Whether a change is waiting for refreshMappingIndex().
/// @return True if the index, or a projection of a binding naming a hosted device, may not match the current song or
/// the devices.
[[nodiscard]] inline bool mappingIndexStale() noexcept {
	return mapping_index_detail::mapping_index_stale.load(std::memory_order_acquire)
	       || mapping_index_detail::mapping_devices_changed.load(std::memory_order_acquire);
}

} // namespace deluge
