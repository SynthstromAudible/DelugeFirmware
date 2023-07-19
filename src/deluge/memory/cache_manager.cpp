#include "cache_manager.h"
#include "io/uart/uart.h"
#include "memory/memory_region.h"
#include "memory/stealable.h"
#include "processing/engines/audio_engine.h"
#include <tuple>

extern bool skipConsistencyCheck;
uint32_t currentTraversalNo = 0;

// Size 0 means don't care, just get any memory.
uint32_t CacheManager::freeSomeStealableMemory(MemoryRegion& region, int totalSizeNeeded, void* thingNotToStealFrom,
                                               int* __restrict__ foundSpaceSize) {

#if TEST_GENERAL_MEMORY_ALLOCATION
	skipConsistencyCheck = true; // Things will not be in an inspectable state during this function call
#endif

	AudioEngine::logAction("freeSomeStealableMemory");

	uint32_t traversalNumberBeforeQueues = currentTraversalNo;

	Stealable* stealable;
	uint32_t newSpaceAddress;
	uint32_t spaceSize;

	int numberReassessed = 0;

	int numRefusedTheft = 0;

	// Go through each queue, one by one
	for (int q = 0; q < NUM_STEALABLE_QUEUES; q++, currentTraversalNo++) {

		// If we already (more or less) know there isn't a long enough run, including neighbouring memory, in this queue, skip it.
		if (stealableClusterQueueLongestRuns[q] < totalSizeNeeded) {
			continue;
		}

		uint32_t longestRunSeenInThisQueue = 0;

		stealable = (Stealable*)stealableClusterQueues[q].getFirst();

startAgain:
		if (!stealable) {
			stealableClusterQueueLongestRuns[q] = longestRunSeenInThisQueue;
			continue; // End of that particular queue - so go to the next one
		}

		// If we've already looked at this one as part of a bigger run, move on
		uint32_t lastTraversalQueue = stealable->lastTraversalNo - traversalNumberBeforeQueues;
		if (lastTraversalQueue <= q) {

			// If that previous look was in a different queue, it won't have been included in longestRunSeenInThisQueue, so we have to invalidate that.
			// TODO: could we just lower it to the longest-run record for that other queue? Yes, done.
			if (lastTraversalQueue < q
			    && longestRunSeenInThisQueue < stealableClusterQueueLongestRuns[lastTraversalQueue]) {
				longestRunSeenInThisQueue = stealableClusterQueueLongestRuns[lastTraversalQueue];
			}

moveOn:
			stealable = (Stealable*)stealableClusterQueues[q].getNext(stealable);
			goto startAgain;
		}

		// If we're forbidden from stealing from a particular thing (usually SampleCache), then make sure we don't
		if (!stealable->mayBeStolen(thingNotToStealFrom)) {
			numRefusedTheft++;

			// If we've done this loads of times, it'll be seriously hurting CPU usage. There's a particular case to be careful of - if project
			// contains just one long pitch-adjusted sound / AudioClip and nothing else, it'll cache it, but after some number of minutes,
			// it'll run out of new Clusters to write the cache to, and it'll start trying to steal from the cache-Cluster queue, and hit all of these ones
			// of its own at the same time.
			if (numRefusedTheft >= 512) {
				AudioEngine::bypassCulling = true;
			}

			goto moveOn;
		}

		// If we're not in the last queue, and we haven't tried this too many times yet, check whether it was actually in the right queue
		if (q < NUM_STEALABLE_QUEUES - 1 && numberReassessed < 4) {
			numberReassessed++;

			int appropriateQueue = stealable->getAppropriateQueue();

			// If it was in the wrong queue, put it in the right queue and start again with the next one in our queue
			if (appropriateQueue > q) {

				Uart::print("changing queue from ");
				Uart::print(q);
				Uart::print(" to ");
				Uart::println(appropriateQueue);

				Stealable* next = (Stealable*)stealableClusterQueues[q].getNext(stealable);

				stealable->remove();
				stealableClusterQueues[appropriateQueue].addToEnd(stealable);
				stealableClusterQueueLongestRuns[appropriateQueue] = 0xFFFFFFFF;

				stealable = next;
				goto startAgain;
			}
		}

		// Ok, we've got one Stealable
		uint32_t* __restrict__ header = (uint32_t*)((uint32_t)stealable - 4);
		spaceSize = (*header & SPACE_SIZE_MASK);

		stealable->lastTraversalNo = currentTraversalNo;

		// How much additional space would we need on top of this Stealable?
		int amountToExtend = totalSizeNeeded - spaceSize;

		newSpaceAddress = (uint32_t)stealable;

		// If that one Stealable alone was big enough, that's great
		if (amountToExtend <= 0) {
			goto foundIt;
		}

		// Otherwise, see if available neighbouring memory adds up to make enough in total
		NeighbouringMemoryGrabAttemptResult result = region.attemptToGrabNeighbouringMemory(
		    stealable, spaceSize, amountToExtend, amountToExtend, thingNotToStealFrom, currentTraversalNo, true);
		// We also told that function to steal the initial main Stealable we are looking at, once it has ascertained that there is enough memory in total.
		// Previously I attempted to have it steal everything but that central Stealable, and we would steal that afterwards, down below, but this could go wrong
		// as thefts occurring in the above call to attemptToGrabNeighbouringMemory() could themselves cause other memory to be deallocated or shortened -
		// and what if this happened to our main, central Stealable before we actually steal it?
		// This was certainly a problem in automated testing, though I haven't quite wrapped my head around whether this would quite occur under real operation -
		// but oh well, there is no harm in taking the safe option.

		// If that couldn't be done (in which case the original, central Stealable won't have been stolen either), move on to next Stealable to assess
		if (!result.address) {
			if (result.longestRunFound > longestRunSeenInThisQueue) {
				longestRunSeenInThisQueue = result.longestRunFound;
			}
			goto moveOn;
		}

		newSpaceAddress = result.address;

		spaceSize += result.amountsExtended[0] + result.amountsExtended[1];

		Uart::println("stole and grabbed neighbouring stuff too...........");
		goto stolenIt;
	}

#if TEST_GENERAL_MEMORY_ALLOCATION
	skipConsistencyCheck = false;
#endif
	AudioEngine::logAction("/freeSomeStealableMemory nope");
	return 0;

foundIt:
	stealable->steal(
	    "i007"); // Warning - for perc cache Cluster, stealing one can cause it to want to allocate more memory for its list of zones
	stealable->~Stealable();

stolenIt:
	*foundSpaceSize = spaceSize;
#if TEST_GENERAL_MEMORY_ALLOCATION
	skipConsistencyCheck = false;
#endif

	AudioEngine::logAction("/freeSomeStealableMemory succes");

	return newSpaceAddress;
}
