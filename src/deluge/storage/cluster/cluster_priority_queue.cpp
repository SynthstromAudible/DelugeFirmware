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
#include "definitions_cxx.hpp"
#include "io/debug/print.h"

class Cluster;

ClusterPriorityQueue::ClusterPriorityQueue()
    : OrderedResizeableArrayWith32bitKey(sizeof(PriorityQueueElement), 32, 31) {
}

// Returns error
int32_t ClusterPriorityQueue::add(Cluster* cluster, uint32_t priorityRating) {
	int32_t i = insertAtKey((int32_t)cluster);
	if (i == -1) {
		return ERROR_INSUFFICIENT_RAM;
	}

	PriorityQueueElement* element = (PriorityQueueElement*)getElementAddress(i);
	element->priorityRating = priorityRating;
	element->cluster = cluster;
	return NO_ERROR;
}

Cluster* ClusterPriorityQueue::grabHead() {
	if (!numElements) {
		return NULL;
	}
	Cluster* toReturn = ((PriorityQueueElement*)getElementAddress(0))->cluster;
	deleteAtIndex(0);
	return toReturn;
}

// Returns whether it was present
bool ClusterPriorityQueue::removeIfPresent(Cluster* cluster) {
	for (int32_t i = 0; i < numElements; i++) {
		PriorityQueueElement* element = (PriorityQueueElement*)getElementAddress(i);
		if (element->cluster == cluster) {
			deleteAtIndex(i);
			return true;
		}
	}

	return false;
}

bool ClusterPriorityQueue::checkPresent(Cluster* cluster) {
	for (int32_t i = 0; i < numElements; i++) {
		PriorityQueueElement* element = (PriorityQueueElement*)getElementAddress(i);
		if (element->cluster == cluster) {
			return true;
		}
	}

	return false;
}
