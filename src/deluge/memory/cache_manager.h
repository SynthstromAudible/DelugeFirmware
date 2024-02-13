#pragma once

#include "definitions_cxx.hpp"
#include "memory/stealable.h"
#include "util/container/list/bidirectional_linked_list.h"
#include <array>
#include <cstddef>

class MemoryRegion;

class CacheManager {
public:
	CacheManager() = default;

	BidirectionalLinkedList& queue(size_t idx) { return reclamation_queue_.at(idx); }

	uint32_t& longest_runs(size_t idx) { return longest_runs_.at(idx); }

	void PutAtEndOfQueue(size_t q, Stealable* stealable) {
		reclamation_queue_[q].addToEnd(stealable);
		longest_runs_[q] = 0xFFFFFFFF; // TODO: actually investigate neighbouring memory "run".
	}

	/// adds to start of queue - logic is that a recently freed sample is unlikely to be immediately needed again
	void QueueForReclamation(size_t q, Stealable* stealable) {
		reclamation_queue_[q].addToStart(stealable);
		longest_runs_[q] = 0xFFFFFFFF; // TODO: actually investigate neighbouring memory "run".
	}

	uint32_t ReclaimMemory(MemoryRegion& region, int32_t totalSizeNeeded, void* thingNotToStealFrom,
	                       int32_t* __restrict__ foundSpaceSize);

private:
	std::array<BidirectionalLinkedList, NUM_STEALABLE_QUEUES> reclamation_queue_;

	// Keeps track, semi-accurately, of biggest runs of memory that could be stolen. In a perfect world, we'd have a
	// second index on stealableClusterQueues[q], for run length. Although even that wouldn't automatically reflect
	// changes to run lengths as neighbouring memory is allocated.
	std::array<uint32_t, NUM_STEALABLE_QUEUES> longest_runs_;
};
