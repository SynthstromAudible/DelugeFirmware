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

#include "cluster.h"
#include "util/containers.h"
#include <cstdint>
#include <limits>
#include <ranges>

using Priority = uint32_t;
using qcluster = std::pair<Priority, Cluster*>;

// comparator returns true if elements need to be swapped
class Compare {
public:
	bool operator()(const qcluster& first, const qcluster& second) const { return first.first > second.first; }
};
class ClusterPriorityQueue : public std::priority_queue<qcluster, deluge::fast_vector<qcluster>, Compare> {

public:
	Error enqueueCluster(Cluster& cluster, uint32_t priorityRating) {
		this->push(std::pair(priorityRating, &cluster));
		return Error::NONE;
	}
	[[nodiscard]] constexpr Cluster* front() const { return this->top().second; }
	[[nodiscard]] Cluster* getNext() {
		Cluster* cluster = nullptr;
		while (!this->empty()) {
			cluster = this->front();
			this->pop();
			if (cluster != nullptr) {
				// if this cluster is still wanted then we'll return it
				if ((!cluster->unloadable) && cluster->numReasonsToBeLoaded > 0) {
					return cluster;
				}
				// otherwise get rid of it and continue to the next one
				cluster->destroy();
			}
		}
		return nullptr;
	}

	/* This is currently a consequence of how priorities are calculated (using full 32bits) next-gen getPriorityRating
	 * should return an int32_t */
	[[nodiscard]] constexpr bool hasAnyLowestPriority() const {
		return !c.empty() && c.rbegin()->first == std::numeric_limits<uint32_t>::max();
	}

	bool erase(const Cluster* cluster) {
		for (auto& val : this->c | std::views::values) {
			if (val == cluster) {
				val = nullptr;
				return true;
			}
		}
		return false;
	};
};
