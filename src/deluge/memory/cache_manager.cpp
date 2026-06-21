#include "cache_manager.h"
#include "definitions_cxx.hpp"
#include "memory/stealable.h"
#include "processing/engines/audio_engine.h"
#include <cstdint>

void* CacheManager::reclaimOne(void* thingNotToStealFrom) {
	int32_t numRefusedTheft = 0;
	int32_t numberReassessed = 0;

	// Priority order: queue 0 (NO_SONG) first to evict, queue 9 (CURRENT_SONG) last.
	for (size_t q = 0; q < kNumStealableQueue; q++) {
		auto queue = static_cast<StealableQueue>(q);
		auto* stealable = static_cast<Stealable*>(reclamation_queue_[q].getFirst());
		while (stealable != nullptr) {
			// Don't steal pinned/protected blocks (numReasonsToBeLoaded, the cluster
			// a voice is mid-read of via thingNotToStealFrom).
			if (!stealable->mayBeStolen(thingNotToStealFrom)) {
				numRefusedTheft++;
				if (numRefusedTheft >= 512) {
					AudioEngine::bypassCulling = true;
				}
				stealable = static_cast<Stealable*>(reclamation_queue_[q].getNext(stealable));
				continue;
			}

			// Lazy re-sort: if it belongs in a more-valuable (later) queue, move it.
			if (q < kNumStealableQueue - 1 && numberReassessed < 4) {
				numberReassessed++;
				StealableQueue appropriate = stealable->getAppropriateQueue();
				if (appropriate > queue) {
					auto* next = static_cast<Stealable*>(reclamation_queue_[q].getNext(stealable));
					stealable->remove();
					this->QueueForReclamation(appropriate, stealable);
					stealable = next;
					continue;
				}
			}

			// Victim. steal() notifies the owner; ~Stealable() unlinks it from the
			// queue. The memory is still valid — the caller returns it to the heap.
			stealable->steal("i007");
			stealable->~Stealable();
			return stealable;
		}
	}
	return nullptr;
}
