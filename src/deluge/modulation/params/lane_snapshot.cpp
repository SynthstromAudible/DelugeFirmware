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

#include "modulation/params/lane_snapshot.h"

#include "memory/heaps.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <type_traits>

namespace deluge::modulation {

static_assert(std::is_trivially_copyable_v<project::ParamNode>);
static_assert(sizeof(PublishedLane) % alignof(project::ParamNode) == 0);
static_assert(sizeof(LaneSnapshot) % alignof(LaneSnapshot::Entry) == 0);

namespace {
const project::ParamNode* nodesOf(const PublishedLane* lane) {
	return reinterpret_cast<const project::ParamNode*>(lane + 1);
}
} // namespace

project::LaneView PublishedLane::view() const {
	return {.base = base, .nodes = std::span<const project::ParamNode>(nodesOf(this), count)};
}

bool PublishedLane::equals(const project::DocAutoParam& lane) const {
	if (base != lane.current_value || count != static_cast<uint32_t>(lane.nodes.size())) {
		return false;
	}
	const project::ParamNode* mine = nodesOf(this);
	for (uint32_t i = 0; i < count; i++) {
		const project::ParamNode& theirs = lane.nodes[static_cast<int32_t>(i)];
		if (mine[i].pos != theirs.pos || mine[i].value != theirs.value || mine[i].interpolated != theirs.interpolated) {
			return false;
		}
	}
	return true;
}

const PublishedLane* PublishedLane::copyOf(const project::DocAutoParam& lane) {
	const auto count = static_cast<uint32_t>(lane.nodes.size());
	void* memory = deluge::memory::alloc_sdram(sizeof(PublishedLane) + (count * sizeof(project::ParamNode)));
	if (memory == nullptr) {
		return nullptr;
	}
	auto* copy = new (memory) PublishedLane{.base = lane.current_value, .count = count};
	if (count != 0) {
		std::memcpy(static_cast<void*>(copy + 1), &*lane.nodes.begin(), count * sizeof(project::ParamNode));
	}
	return copy;
}

void PublishedLane::destroy(void* lane) {
	deluge::memory::dealloc(lane);
}

const PublishedLane* LaneSnapshot::find(int32_t key) const {
	const std::span<const Entry> all = entries();
	auto it = std::ranges::lower_bound(all, key, {}, &Entry::key);
	return (it != all.end() && it->key == key) ? it->lane : nullptr;
}

std::span<const LaneSnapshot::Entry> LaneSnapshot::entries() const {
	return {reinterpret_cast<const Entry*>(this + 1), count};
}

std::span<LaneSnapshot::Entry> LaneSnapshot::entriesToFill() {
	return {reinterpret_cast<Entry*>(this + 1), count};
}

LaneSnapshot* LaneSnapshot::make(uint32_t count) {
	void* memory = deluge::memory::alloc_sdram(sizeof(LaneSnapshot) + (count * sizeof(Entry)));
	if (memory == nullptr) {
		return nullptr;
	}
	auto* snapshot = new (memory) LaneSnapshot{};
	snapshot->count = count;
	return snapshot;
}

void LaneSnapshot::destroy(void* snapshot) {
	deluge::memory::dealloc(snapshot);
}

} // namespace deluge::modulation
