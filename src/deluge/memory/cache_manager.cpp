#include "cache_manager.h"
#include "definitions_cxx.hpp"
#include "io/debug/log.h"
#include "memory/memory_region.h"
#include "memory/stealable.h"
#include "processing/engines/audio_engine.h"

extern bool skipConsistencyCheck;
uint32_t currentTraversalNo = 0;

// Size 0 means don't care, just get any memory.
uint32_t CacheManager::ReclaimMemory(MemoryRegion& region, int32_t totalSizeNeeded, void* thingNotToStealFrom,
                                     int32_t* __restrict__ foundSpaceSize) {

#if TEST_GENERAL_MEMORY_ALLOCATION
	skipConsistencyCheck = true; // Things will not be in an inspectable state during this function call
#endif

	AudioEngine::logAction("CacheManager::reclaim");

	uint32_t traversalNumberBeforeQueues = currentTraversalNo;

	Stealable* stealable = nullptr;
	uint32_t newSpaceAddress = 0;
	uint32_t spaceSize = 0;

	int32_t numberReassessed = 0;

	int32_t numRefusedTheft = 0;

	bool found = false;
	bool stolen = false;

	// Go through each queue, one by one
	for (size_t q = 0; q < NUM_STEALABLE_QUEUES; q++) {
		// base case, if we've found or stolen enough memory, break
		if (found || stolen) {
			break;
		}

		// If we already (more or less) know there isn't a long enough run, including neighbouring memory, in this
		// queue, skip it.
		if (longest_runs_[q] < totalSizeNeeded) {
			continue;
		}

		uint32_t longestRunSeenInThisQueue = 0;

		stealable = static_cast<Stealable*>(reclamation_queue_[q].getFirst());
		while (stealable != nullptr) {
			// If we've already looked at this one as part of a bigger run, move on
			// this works because the uint cast makes negatives high numbers instead
			uint32_t lastTraversalQueue = stealable->lastTraversalNo - traversalNumberBeforeQueues;
			if (lastTraversalQueue <= q) {

				// If that previous look was in a different queue, it won't have been included in
				// longestRunSeenInThisQueue, so we have to invalidate that.
				// TODO: could we just lower it to the longest-run record for that other queue? Yes, done.
				if (lastTraversalQueue < q && longestRunSeenInThisQueue < longest_runs_[lastTraversalQueue]) {
					longestRunSeenInThisQueue = longest_runs_[lastTraversalQueue];
				}
				stealable = static_cast<Stealable*>(reclamation_queue_[q].getNext(stealable));
				continue;
			}

			// If we're forbidden from stealing from a particular thing (usually SampleCache), then make sure we don't
			// TODO: this should never happen
			if (!stealable->mayBeStolen(thingNotToStealFrom)) {
				numRefusedTheft++;

				// If we've done this loads of times, it'll be seriously hurting CPU usage. There's a particular case to
				// be careful of - if project contains just one long pitch-adjusted sound / AudioClip and nothing else,
				// it'll cache it, but after some number of minutes, it'll run out of new Clusters to write the cache
				// to, and it'll start trying to steal from the cache-Cluster queue, and hit all of these ones of its
				// own at the same time.
				if (numRefusedTheft >= 512) {
					AudioEngine::logAction("bypass culling - refused 512 times");
					AudioEngine::bypassCulling = true;
				}
				stealable = static_cast<Stealable*>(reclamation_queue_[q].getNext(stealable));
				continue;
			}

			// If we're not in the last queue, and we haven't tried this too many times yet, check whether it was
			// actually in the right queue
			if (q < NUM_STEALABLE_QUEUES - 1 && numberReassessed < 4) {
				numberReassessed++;

				StealableQueue appropriateQueue = stealable->getAppropriateQueue();

				// If it was in the wrong queue, put it in the right queue and start again with the next one in our
				// queue
				if (appropriateQueue > q) {

					D_PRINTLN("changing queue from  %d  to  %d", q, appropriateQueue);

					auto* next = static_cast<Stealable*>(reclamation_queue_[q].getNext(stealable));

					stealable->remove();
					this->QueueForReclamation(appropriateQueue, stealable);

					stealable = next;
					continue;
				}
			}

			// Ok, we've got one Stealable
			uint32_t* __restrict__ header = (uint32_t*)((uint32_t)stealable - 4);
			spaceSize = (*header & SPACE_SIZE_MASK);

			stealable->lastTraversalNo = currentTraversalNo;

			// How much additional space would we need on top of this Stealable?
			int32_t amountToExtend = totalSizeNeeded - spaceSize;

			newSpaceAddress = (uint32_t)stealable;

			// If that one Stealable alone was big enough, that's great
			if (amountToExtend <= 0) {
				// need to reset this since it's getting stolen
				longestRunSeenInThisQueue = 0xFFFFFFFF;
				found = true;
				break;
			}

			// Otherwise, see if available neighbouring memory adds up to make enough in total
			NeighbouringMemoryGrabAttemptResult result = region.attemptToGrabNeighbouringMemory(
			    stealable, spaceSize, amountToExtend, amountToExtend, thingNotToStealFrom, currentTraversalNo, true);

			// We also told that function to steal the initial main Stealable we are looking at, once it has ascertained
			// that there is enough memory in total. Previously I attempted to have it steal everything but that central
			// Stealable, and we would steal that afterwards, down below, but this could go wrong as thefts occurring in
			// the above call to attemptToGrabNeighbouringMemory() could themselves cause other memory to be deallocated
			// or shortened - and what if this happened to our main, central Stealable before we actually steal it? This
			// was certainly a problem in automated testing, though I haven't quite wrapped my head around whether this
			// would quite occur under real operation - but oh well, there is no harm in taking the safe option.

			// If that couldn't be done (in which case the original, central Stealable won't have been stolen either),
			// move on to next Stealable to assess
			if (!result.address) {
				if (result.longestRunFound > longestRunSeenInThisQueue) {
					longestRunSeenInThisQueue = result.longestRunFound;
				}
				stealable = static_cast<Stealable*>(reclamation_queue_[q].getNext(stealable));
				continue;
			}
			else {
				// reset this since it's getting stolen
				longestRunSeenInThisQueue = 0xFFFFFFFF;
			}

			newSpaceAddress = result.address;

			spaceSize += result.amountsExtended[0] + result.amountsExtended[1];

			D_PRINTLN("stole and grabbed neighbouring stuff too...........");
			AudioEngine::bypassCulling = true; // Paul: We don't want our samples to drop out because of this maneuver
			stolen = true;
			break;
		}

		longest_runs_[q] = longestRunSeenInThisQueue;

		// End of that particular queue - so go to the next one

		currentTraversalNo++;
	}

	if (!(found || stolen)) {
#if TEST_GENERAL_MEMORY_ALLOCATION
		skipConsistencyCheck = false;
#endif
		AudioEngine::logAction("/CacheManager::reclaim nope");
		return 0;
	}

	if (found && !stolen) {
		// Warning - for perc cache Cluster, stealing one can cause it to want to allocate more memory for its list of
		// zones
		stealable->steal("i007");
		stealable->~Stealable();
	}

	// At this point we have either found or stolen to be true
	*foundSpaceSize = spaceSize;
#if TEST_GENERAL_MEMORY_ALLOCATION
	skipConsistencyCheck = false;
#endif

	AudioEngine::logAction("/CacheManager::reclaim success");

	return newSpaceAddress;
}
