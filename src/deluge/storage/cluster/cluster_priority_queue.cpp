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

#include "storage/cluster/cluster_priority_queue.h"
#include "definitions.h"
#include "definitions_cxx.hpp"
#include "util/exceptions.h"
#include <cstdlib>

class Cluster;

// Returns error
Error ClusterPriorityQueue::push(Cluster& cluster, uint32_t priority) {
	if (auto search = queued_clusters_.find(&cluster); search != queued_clusters_.end()) {
		priority_map_.erase({search->second, &cluster});
	}

	try {
		priority_map_.insert({priority, &cluster});
		queued_clusters_[&cluster] = priority;
	} catch (deluge::exception e) {
		if (e == deluge::exception::BAD_ALLOC) {
			return Error::INSUFFICIENT_RAM;
		}
		FREEZE_WITH_ERROR("EXPQ");
	}
	return Error::NONE;
}
