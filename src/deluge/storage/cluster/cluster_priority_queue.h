/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "util/containers.h"
#include <cstdint>
#include <limits>

class Cluster;

class ClusterPriorityQueue {
	using Priority = uint32_t;

public:
	ClusterPriorityQueue() = default;

	Error push(Cluster& cluster, uint32_t priorityRating);
	constexpr Cluster& front() const { return *priority_map_.begin()->second; }

	constexpr void pop() {
		auto it = priority_map_.begin();
		priority_map_.erase(it);
		queued_clusters_.erase(it->second);
	}

	constexpr bool empty() const { return queued_clusters_.empty() || priority_map_.empty(); }
	constexpr size_t erase(Cluster& cluster) {
		if (auto search = queued_clusters_.find(&cluster); search != queued_clusters_.end()) {
			priority_map_.erase({search->second, &cluster});
			queued_clusters_.erase(&cluster);
			return 1;
		}
		return 0;
	}

	/* This is currently a consequence of how priorities are calculated (using full 32bits) next-gen getPriorityRating
	 * should return an int32_t */
	constexpr bool hasAnyLowestPriority() const {
		return !priority_map_.empty() && priority_map_.rbegin()->first == std::numeric_limits<uint32_t>::max();
	}

private:
	deluge::fast_set<std::pair<Priority, Cluster*>> priority_map_;
	deluge::fast_unordered_map<Cluster*, Priority> queued_clusters_;
};
