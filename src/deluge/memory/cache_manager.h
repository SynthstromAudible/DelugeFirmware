#pragma once

#include "definitions_cxx.hpp"
#include "memory/stealable.h"
#include "util/container/list/bidirectional_linked_list.h"
#include "util/misc.h"
#include <array>
#include <cstddef>

// Owns the stealable-eviction policy for the unified SDRAM heap: the priority
// queues of reclaimable blocks (clusters + other caches) and the victim selection
// that the Rust heap's reclaim hook drives. The allocator (deluge_alloc / the
// GeneralMemoryAllocator coordinator) owns *mechanism*; this owns *policy*.
class CacheManager {
public:
	CacheManager() = default;

	BidirectionalLinkedList& queue(StealableQueue destination) {
		return reclamation_queue_.at(util::to_underlying(destination));
	}

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
	}

	// The deluge_alloc reclaim-hook path: pick the coldest unpinned stealable across
	// the priority queues, steal() + destroy it, and return its memory for the caller
	// to hand back to the heap (deluge_free / the slab). Returns nullptr if nothing is
	// evictable. The TLSF heap owns contiguity, so there is no neighbour-grab /
	// run-length / size machinery — deluge_alloc's retry loop drives this one victim
	// at a time.
	void* reclaimOne(void* thingNotToStealFrom);

private:
	std::array<BidirectionalLinkedList, kNumStealableQueue> reclamation_queue_;
};
