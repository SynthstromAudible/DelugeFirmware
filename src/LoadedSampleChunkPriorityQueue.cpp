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

#include <LoadedSampleChunkPriorityQueue.h>
#include "definitions.h"
#include "uart.h"

class Cluster;



LoadedSampleChunkPriorityQueue::LoadedSampleChunkPriorityQueue() :
		OrderedResizeableArrayWith32bitKey(sizeof(PriorityQueueElement), 32, 31)
{
}


// Returns error
int LoadedSampleChunkPriorityQueue::add(Cluster* loadedSampleChunk, uint32_t priorityRating) {
	int i = insertAtKey((int32_t)loadedSampleChunk);
	if (i == -1) return ERROR_INSUFFICIENT_RAM;

	PriorityQueueElement* element = (PriorityQueueElement*)getElementAddress(i);
	element->priorityRating = priorityRating;
	element->loadedSampleChunk = loadedSampleChunk;
	return NO_ERROR;
}

Cluster* LoadedSampleChunkPriorityQueue::grabHead() {
	if (!numElements) return NULL;
	Cluster* toReturn = ((PriorityQueueElement*)getElementAddress(0))->loadedSampleChunk;
	deleteAtIndex(0);
	return toReturn;
}

// Returns whether it was present
bool LoadedSampleChunkPriorityQueue::removeIfPresent(Cluster* loadedSampleChunk) {
	for (int i = 0; i < numElements; i++) {
		PriorityQueueElement* element = (PriorityQueueElement*)getElementAddress(i);
		if (element->loadedSampleChunk == loadedSampleChunk) {
			deleteAtIndex(i);
			return true;
		}
	}

	return false;
}


bool LoadedSampleChunkPriorityQueue::checkPresent(Cluster* loadedSampleChunk) {
	for (int i = 0; i < numElements; i++) {
		PriorityQueueElement* element = (PriorityQueueElement*)getElementAddress(i);
		if (element->loadedSampleChunk == loadedSampleChunk) {
			return true;
		}
	}

	return false;
}
