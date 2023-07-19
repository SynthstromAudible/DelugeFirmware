#pragma once

#include "definitions.h"
#include "memory/stealable.h"
#include "util/container/list/bidirectional_linked_list.h"
#include <array>
#include <cstddef>

class MemoryRegion;

class CacheManager {
public:
	BidirectionalLinkedList& queue(size_t idx) { return stealableClusterQueues.at(idx); }

	uint32_t& longest_runs(size_t idx) { return stealableClusterQueueLongestRuns.at(idx); }

	uint32_t freeSomeStealableMemory(MemoryRegion& region, int totalSizeNeeded, void* thingNotToStealFrom,
	                                 int* __restrict__ foundSpaceSize);


	void enqueue(size_t q, Stealable *stealable) {
		stealableClusterQueues[q].addToEnd(stealable);
		stealableClusterQueueLongestRuns[q] = 0xFFFFFFFF; // TODO: actually investigate neighbouring memory "run".
	}

private:
	std::array<BidirectionalLinkedList, NUM_STEALABLE_QUEUES> stealableClusterQueues;

	// Keeps track, semi-accurately, of biggest runs of memory that could be stolen. In a perfect world, we'd have a second
	// index on stealableClusterQueues[q], for run length. Although even that wouldn't automatically reflect changes to run
	// lengths as neighbouring memory is allocated.
	std::array<uint32_t, NUM_STEALABLE_QUEUES> stealableClusterQueueLongestRuns;
};
