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

#include "definitions_cxx.hpp"
#include "deluge-project/lane_view.h"

#include <cstdint>
#include <span>

namespace deluge::modulation {

/// @brief One lane as the render plays it: its base and nodes, immutable once made. One allocation in SDRAM, the nodes
///        after the header.
struct PublishedLane {
	int32_t base;
	uint32_t count;

	/// @brief The lane as a read-only view.
	[[nodiscard]] project::LaneView view() const;
	/// @brief Whether it holds exactly @p lane: the same base and the same nodes.
	[[nodiscard]] bool equals(const project::DocAutoParam& lane) const;

	/// @brief [interaction] A copy of @p lane.
	/// @return The copy, or nullptr when out of memory.
	static const PublishedLane* copyOf(const project::DocAutoParam& lane);
	/// @brief Free a lane made by copyOf(); the destroy function AudioEngine::retireMemory() takes.
	static void destroy(void* lane);
};

/// @brief A collection's lanes as the render plays them, immutable once published: which of its lanes are automated, in
///        the collection summary's word layout, and each automated lane by the key the collection's cursors use. One
///        allocation in SDRAM, the entries after the header.
struct alignas(alignof(void*)) LaneSnapshot {
	struct Entry {
		int32_t key; ///< param index (ParamSet), cable index (PatchCableSet), CC number (MIDIParamCollection)
		const PublishedLane* lane;
	};

	uint32_t automated[kMaxNumUnsignedIntegerstoRepAllParams];
	uint32_t count; ///< entries, ascending by key; only lanes with nodes

	/// @brief The lane published under @p key.
	/// @return It, or nullptr when that lane has no nodes.
	[[nodiscard]] const PublishedLane* find(int32_t key) const;
	/// @brief Every entry, ascending by key.
	[[nodiscard]] std::span<const Entry> entries() const;

	/// @brief [interaction] Room for @p count entries, the bits cleared, for a builder to fill before it is published.
	/// @return It, or nullptr when out of memory.
	static LaneSnapshot* make(uint32_t count);
	/// @brief The entries of a snapshot being built.
	[[nodiscard]] std::span<Entry> entriesToFill();
	/// @brief Free a snapshot made by make() (not its lanes); the destroy function AudioEngine::retireMemory() takes.
	static void destroy(void* snapshot);
};

} // namespace deluge::modulation
