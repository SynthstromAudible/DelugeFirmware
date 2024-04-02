#pragma once

#include "definitions_cxx.hpp"
#include "memory/stealable.h"
#include "util/container/list/bidirectional_linked_list.h"
#include "util/misc.h"
#include <array>
#include <cstddef>

class MemoryRegion;

class CacheManager {
public:
	CacheManager() = default;

	BidirectionalLinkedList& queue(StealableQueue destination) {
		return reclamation_queue_.at(util::to_underlying(destination));
	}

	uint32_t& longest_runs(size_t idx) { return longest_runs_.at(idx); }

	/// add a stealable to end of given queue
	void QueueForReclamation(StealableQueue queue, Stealable* stealable) {
		size_t q = util::to_underlying(queue);

		/// Alternatively we could add to start of queue - logic is that a recently freed sample is unlikely
		/// to be immediately needed again. This increases average and max voice counts, but has a problem with medium
		/// memory pressure songs where it tends to prioritize earlier sounds in the song and makes it possible for
		/// later songs to break in. This occurs since there's no mechanism to determine if a sample is going to be used
		/// in the remainder of the song, so if there's not enough memory pressure for all stealable clusters to get
		/// reclaimed the same few just get put on and off the list repeatedly
		reclamation_queue_[q].addToEnd(stealable);
		longest_runs_[q] = 0xFFFFFFFF; // TODO: actually investigate neighbouring memory "run".
	}

	uint32_t ReclaimMemory(MemoryRegion& region, int32_t totalSizeNeeded, void* thingNotToStealFrom,
	                       int32_t* __restrict__ foundSpaceSize);

private:
	std::array<BidirectionalLinkedList, kNumStealableQueue> reclamation_queue_;

	// Keeps track, semi-accurately, of biggest runs of memory that could be stolen. In a perfect world, we'd have a
	// second index on stealableClusterQueues[q], for run length. Although even that wouldn't automatically reflect
	// changes to run lengths as neighbouring memory is allocated.
	std::array<uint32_t, kNumStealableQueue> longest_runs_;
};
