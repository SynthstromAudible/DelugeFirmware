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

#include <AudioEngine.h>
#include <MemoryRegion.h>
#include "Stealable.h"
#include "uart.h"
#include "GeneralMemoryAllocator.h"
#include "mtu_all_cpus.h"
#include "functions.h"
#include "numericdriver.h"

MemoryRegion::MemoryRegion() : emptySpaces(sizeof(EmptySpaceRecord)) {
	numAllocations = 0;
}

void MemoryRegion::setup(void* emptySpacesMemory, int emptySpacesMemorySize, uint32_t regionBegin, uint32_t regionEnd) {
	emptySpaces.setStaticMemory(emptySpacesMemory, emptySpacesMemorySize);

	uint32_t memorySizeWithoutHeaders = regionEnd - regionBegin - 16;

	*(uint32_t*)regionBegin = SPACE_HEADER_ALLOCATED;
	*(uint32_t*)(regionBegin + 4) = SPACE_HEADER_EMPTY | memorySizeWithoutHeaders;

	*(uint32_t*)(regionEnd - 8) = SPACE_HEADER_EMPTY | memorySizeWithoutHeaders;
	*(uint32_t*)(regionEnd - 4) = SPACE_HEADER_ALLOCATED;

	emptySpaces.insertAtIndex(0);
	EmptySpaceRecord* firstRecord = (EmptySpaceRecord*)emptySpaces.getElementAddress(0);
	firstRecord->length = memorySizeWithoutHeaders;
	firstRecord->address = regionBegin + 8;
}

bool seenYet = false;

void MemoryRegion::sanityCheck() {
	int count = 0;
	for (int j = 0; j < emptySpaces.getNumElements(); j++) {
		EmptySpaceRecord* emptySpaceRecord = (EmptySpaceRecord*)emptySpaces.getElementAddress(j);
		if (emptySpaceRecord->address == (uint32_t)0xc0080bc) {
			count++;
		}
	}

	if (count > 1) {
		Uart::println("multiple 0xc0080bc!!!!");
		numericDriver.freezeWithError("BBBB");
	}
	else if (count == 1) {
		if (!seenYet) {
			seenYet = true;
			Uart::println("seen 0xc0080bc");
		}
	}
}

void MemoryRegion::verifyMemoryNotFree(void* address, uint32_t spaceSize) {
	for (int i = 0; i < emptySpaces.getNumElements(); i++) {
		EmptySpaceRecord* emptySpaceRecord = (EmptySpaceRecord*)emptySpaces.getElementAddress(i);
		if (emptySpaceRecord->address == (uint32_t)address) {
			Uart::println("Exact address free!");
			numericDriver.freezeWithError("dddffffd");
		}
		else if (emptySpaceRecord->address <= (uint32_t)address
		         && (emptySpaceRecord->address + emptySpaceRecord->length > (uint32_t)address)) {
			Uart::println("free mem overlap on left!");
			numericDriver.freezeWithError("dddd");
		}
		else if ((uint32_t)address <= (uint32_t)emptySpaceRecord->address
		         && ((uint32_t)address + spaceSize > emptySpaceRecord->address)) {
			Uart::println("free mem overlap on right!");
			numericDriver.freezeWithError("eeee");
		}
	}
}

// Okay this is me being experimental and trying something you're not supposed to do - using static variables in place of stack ones within a function.
// It seemed to give a slight speed up, but it's probably quite circumstantial, and I wouldn't normally do this.
static EmptySpaceRecord emptySpaceToLeft;
static EmptySpaceRecord emptySpaceToRight;
static EmptySpaceRecord* recordToMergeWith;

// Specify the address and size of the actual memory region not including its headers, which this function will write and don't have to contain valid data yet.
// spaceSize can even be 0 or less if you know it's going to get merged.
inline void MemoryRegion::markSpaceAsEmpty(uint32_t address, uint32_t spaceSize, bool mayLookLeft, bool mayLookRight) {

	int biggerRecordSearchFromIndex = 0;
	int insertRangeBegin;

	// Can we merge left?
	if (mayLookLeft) {
		uint32_t* __restrict__ lookLeft = (uint32_t*)(address - 8);
		if ((*lookLeft & SPACE_TYPE_MASK) == SPACE_HEADER_EMPTY) {
			emptySpaceToLeft.length = *lookLeft & SPACE_SIZE_MASK;

			// Expand our empty space region to include this extra space on the left
			spaceSize += emptySpaceToLeft.length + 8;
			address = address - (emptySpaceToLeft.length + 8);
			emptySpaceToLeft.address = address;

			// Set up the default option - that we are going to merge with the left record. This may get overridden below.
			recordToMergeWith = &emptySpaceToLeft;

			// If we're not allowed to also look right, or there's no unused space there, we want to just go directly to replacing this old record
			if (!mayLookRight) goto goingToReplaceOldRecord;
			uint32_t* __restrict__ lookRight = (uint32_t*)(address + spaceSize + 4);
			if ((*lookRight & SPACE_TYPE_MASK) != SPACE_HEADER_EMPTY) goto goingToReplaceOldRecord;

			// If we are still here, we are going to merge right as well as left, so gather a little bit more info
			emptySpaceToRight.length = *lookRight & SPACE_SIZE_MASK;
			spaceSize += emptySpaceToRight.length + 8;
			emptySpaceToRight.address = (uint32_t)lookRight + 4;
			EmptySpaceRecord* recordToDelete = &emptySpaceToRight; // The default, which we may override below.

			// If the "right" space is bigger, delete the "left" one's record
			if (emptySpaceToRight.length > emptySpaceToLeft.length
			    || (emptySpaceToRight.length == emptySpaceToLeft.length
			        && emptySpaceToRight.address
			               > address)) { // address here is same as, and best thought of as, emptySpaceToLeft.address.
				                         // Hang on, wouldn't the space to the right always have a higher address?
				recordToMergeWith = &emptySpaceToRight;
				recordToDelete = &emptySpaceToLeft;
			}
			// Otherwise, do the opposite - delete the "right" space's record. That default was set back up above.

			int nextIndex;
			biggerRecordSearchFromIndex = emptySpaces.searchMultiWordExact((uint32_t*)recordToDelete, &nextIndex);

			if (biggerRecordSearchFromIndex
			    == -1) { // It might not have been found if array got full so there was no record for this empty space.
				biggerRecordSearchFromIndex = nextIndex;
			}
			else {
				// TODO: Ideally we like to combine this deletion with the below reorganisation of records. But this is actually very complicated, and maybe not even worth it.
				emptySpaces.deleteAtIndex(biggerRecordSearchFromIndex);
			}
			goto goingToReplaceOldRecord;
		}
	}

	// Even if we didn't merge left, we may still want to merge right - and in this case, where that is the only merge we are doing,
	// it's much simpler.
	if (mayLookRight) {
		uint32_t* __restrict__ lookRight = (uint32_t*)(address + spaceSize + 4);
		if ((*lookRight & SPACE_TYPE_MASK) == SPACE_HEADER_EMPTY) {
			emptySpaceToRight.length = *lookRight & SPACE_SIZE_MASK;
			spaceSize += emptySpaceToRight.length + 8;
			emptySpaceToRight.address = (uint32_t)lookRight + 4;

			recordToMergeWith = &emptySpaceToRight;
			goto goingToReplaceOldRecord;
		}
	}

	// If we're still here, we're not merging with anything, so just...
	{
		insertRangeBegin = 0;
justInsertRecord:
		// Add the new empty space record.
		EmptySpaceRecord newRecord;
		newRecord.length = spaceSize;
		newRecord.address = address;
		int i = emptySpaces.insertAtKeyMultiWord((uint32_t*)&newRecord, insertRangeBegin);
#if ALPHA_OR_BETA_VERSION
		if (i
		    == -1) { // Array might have gotten full. This has to be coped with. Perhaps in a perfect world we should opt to throw away the smallest empty space to make space for this one if this one is bigger?
			Uart::print("Lost track of empty space in region: ");
			Uart::println(name);
		}
#endif
	}

	if (false) {

goingToReplaceOldRecord:
		int i = emptySpaces.searchMultiWordExact((uint32_t*)recordToMergeWith, &insertRangeBegin,
		                                         biggerRecordSearchFromIndex);
		if (i
		    == -1) { // The record might not exist because there wasn't room to insert it when the empty space was created.
#if ALPHA_OR_BETA_VERSION
			Uart::print("Found orphaned empty space in region: ");
			Uart::println(name);
#endif
			goto justInsertRecord;
		}

		// If there is a "bigger" record, to the right in the array...
		if (i < emptySpaces.getNumElements() - 1) {
			EmptySpaceRecord* nextBiggerRecord = (EmptySpaceRecord*)emptySpaces.getElementAddress(i + 1);
			int howMuchBigger = nextBiggerRecord->length - spaceSize;

			// If that next "bigger" record to the right in the array is actually smaller than our new space, we'll have to rearrange some elements.
			if (howMuchBigger <= 0 && !(!howMuchBigger && nextBiggerRecord->address > address)) {

				EmptySpaceRecord newRecordPreview;
				newRecordPreview.length = spaceSize;
				newRecordPreview.address = address;

				int insertBefore = emptySpaces.searchMultiWord((uint32_t*)&newRecordPreview, GREATER_OR_EQUAL, i + 2);
				emptySpaces.moveElementsLeft(i + 1, insertBefore, 1);
				i = insertBefore - 1;
			}
		}

		EmptySpaceRecord* recordToUpdate = (EmptySpaceRecord*)emptySpaces.getElementAddress(i);
		recordToUpdate->length = spaceSize;
		recordToUpdate->address = address;
	}

	// Update headers and footers
	uint32_t* __restrict__ header = (uint32_t*)(address - 4);
	uint32_t* __restrict__ footer = (uint32_t*)(address + spaceSize);

	uint32_t headerData = SPACE_HEADER_EMPTY | spaceSize;
	*header = headerData;
	*footer = headerData;
}

extern bool skipConsistencyCheck;
uint32_t currentTraversalNo = 0;

// Size 0 means don't care, just get any memory.
uint32_t MemoryRegion::freeSomeStealableMemory(int totalSizeNeeded, void* thingNotToStealFrom,
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
		if (stealableClusterQueueLongestRuns[q] < totalSizeNeeded) continue;

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
			if (numRefusedTheft >= 512) AudioEngine::bypassCulling = true;

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
		if (amountToExtend <= 0) goto foundIt;

		// Otherwise, see if available neighbouring memory adds up to make enough in total
		NeighbouringMemoryGrabAttemptResult result = attemptToGrabNeighbouringMemory(
		    stealable, spaceSize, amountToExtend, amountToExtend, thingNotToStealFrom, currentTraversalNo, true);
		// We also told that function to steal the initial main Stealable we are looking at, once it has ascertained that there is enough memory in total.
		// Previously I attempted to have it steal everything but that central Stealable, and we would steal that afterwards, down below, but this could go wrong
		// as thefts occurring in the above call to attemptToGrabNeighbouringMemory() could themselves cause other memory to be deallocated or shortened -
		// and what if this happened to our main, central Stealable before we actually steal it?
		// This was certainly a problem in automated testing, though I haven't quite wrapped my head around whether this would quite occur under real operation -
		// but oh well, there is no harm in taking the safe option.

		// If that couldn't be done (in which case the original, central Stealable won't have been stolen either), move on to next Stealable to assess
		if (!result.address) {
			if (result.longestRunFound > longestRunSeenInThisQueue) longestRunSeenInThisQueue = result.longestRunFound;
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

// If getBiggestAllocationPossible is true, this will treat requiredSize as a minimum, and otherwise get as much empty RAM as possible. But, it won't "steal" any more than it has to go get that minimum size.
void* MemoryRegion::alloc(uint32_t requiredSize, uint32_t* getAllocatedSize, bool makeStealable,
                          void* thingNotToStealFrom, bool getBiggestAllocationPossible) {

	requiredSize = (requiredSize + 3) & 0b11111111111111111111111111111100; // Jump to 4-byte boundary

	int allocatedSize;
	uint32_t allocatedAddress;
	int i;

	if (!emptySpaces.getNumElements()) goto noEmptySpace;

	if (getBiggestAllocationPossible) {
		i = emptySpaces.getNumElements() - 1;
		if (emptySpaces.getKeyAtIndex(i) < requiredSize) goto noEmptySpace;
		goto gotEmptySpace;
	}

	// Here we're doing a search just on one 32-bit word of the key (that's "length of empty space").
	i = emptySpaces.search(requiredSize, GREATER_OR_EQUAL);

	// If found an empty space big enough...
	if (i < emptySpaces.getNumElements()
#if 0 && EST_GENERAL_MEMORY_ALLOCATION
			&& getRandom255() >= 10
#endif
	) {
gotEmptySpace:
		EmptySpaceRecord* emptySpaceRecord = (EmptySpaceRecord*)emptySpaces.getElementAddress(i);

		allocatedSize = emptySpaceRecord->length;
		allocatedAddress = emptySpaceRecord->address;

		if (getBiggestAllocationPossible) {
usedWholeSpace:
			emptySpaces.deleteAtIndex(i);
		}
		else {
			int extraSpaceSizeWithoutItsHeaders = allocatedSize - requiredSize - 8;
			if (extraSpaceSizeWithoutItsHeaders <= 0) goto usedWholeSpace;

			allocatedSize = requiredSize;

			uint32_t extraSpaceAddress = allocatedAddress + allocatedSize + 8;

			uint32_t* __restrict__ header = (uint32_t*)((uint32_t)extraSpaceAddress - 4);
			uint32_t* __restrict__ footer = (uint32_t*)((uint32_t)extraSpaceAddress + extraSpaceSizeWithoutItsHeaders);

			// Update headers and footers
			uint32_t headerData = SPACE_HEADER_EMPTY | extraSpaceSizeWithoutItsHeaders;
			*header = headerData;
			*footer = headerData;

			// Hopefully we can just update the same empty space record.
			// We definitely can if it was the leftmost record (smallest empty space).
			if (!i) goto justUpdateRecord;

			{
				// Or even if it wasn't the leftmost record, we might still be able to just simply update - if our new value
				// is still bigger than the record to the left.
				EmptySpaceRecord* nextSmallerRecord = (EmptySpaceRecord*)emptySpaces.getElementAddress(i - 1);
				int howMuchBiggerStill = extraSpaceSizeWithoutItsHeaders - nextSmallerRecord->length;
				if (howMuchBiggerStill > 0 || (!howMuchBiggerStill && extraSpaceAddress > nextSmallerRecord->address))
					goto justUpdateRecord;

				// Okay, if we're here, we have to rearrange some records.
				// Find the best empty space
				EmptySpaceRecord searchThing;
				searchThing.length = extraSpaceSizeWithoutItsHeaders;
				searchThing.address = extraSpaceAddress;
				int insertAt = emptySpaces.searchMultiWord((uint32_t*)&searchThing, GREATER_OR_EQUAL, 0, i);

				emptySpaces.moveElementsRight(insertAt, i, 1);

				emptySpaceRecord = (EmptySpaceRecord*)emptySpaces.getElementAddress(insertAt);
			}
justUpdateRecord:
			emptySpaceRecord->length = extraSpaceSizeWithoutItsHeaders;
			emptySpaceRecord->address = extraSpaceAddress;
		}
	}

	// Or if no empty space big enough, try stealing some memory
	else {
noEmptySpace:
		allocatedAddress = freeSomeStealableMemory(requiredSize, thingNotToStealFrom, &allocatedSize);
		if (!allocatedAddress) {
			//Uart::println("nothing to steal.........................");
			return NULL;
		}

#if 0 && TEST_GENERAL_MEMORY_ALLOCATION
		if (allocatedSize < requiredSize) {
			Uart::println("freeSomeStealableMemory() got too little memory");
			while (1);
		}
#endif

		if (!getBiggestAllocationPossible) {
			// See if there was some extra space left over
			int extraSpaceSizeWithoutItsHeaders = allocatedSize - requiredSize - 8;
			if (requiredSize && extraSpaceSizeWithoutItsHeaders > 0) {
				allocatedSize = requiredSize;
				markSpaceAsEmpty(allocatedAddress + allocatedSize + 8, extraSpaceSizeWithoutItsHeaders, false, false);
			}
		}
	}

	uint32_t* __restrict__ header = (uint32_t*)(allocatedAddress - 4);
	uint32_t* __restrict__ footer = (uint32_t*)(allocatedAddress + allocatedSize);

	uint32_t headerData = (makeStealable ? SPACE_HEADER_STEALABLE : SPACE_HEADER_ALLOCATED) | allocatedSize;
	*header = headerData;
	*footer = headerData;

	if (getAllocatedSize) *getAllocatedSize = allocatedSize;

#if TEST_GENERAL_MEMORY_ALLOCATION
	numAllocations++;
#endif

	return (void*)allocatedAddress;
}

// Returns new size
uint32_t MemoryRegion::shortenRight(void* address, uint32_t newSize) {

	newSize = getMax(newSize, 4);
	newSize = (newSize + 3) & 0b11111111111111111111111111111100; // Round new size up to 4-byte boundary

	uint32_t* __restrict__ header = (uint32_t*)((char*)address - 4);
	uint32_t oldAllocatedSize = *header & SPACE_SIZE_MASK;
	uint32_t allocationType = *header & SPACE_TYPE_MASK;

	uint32_t* __restrict__ lookRight = (uint32_t*)((uint32_t)address + oldAllocatedSize
	                                               + 4); // Looking to what's directly right of our old allocated space

	int newSizeLowerLimit = oldAllocatedSize;
	if ((*lookRight & SPACE_TYPE_MASK) != SPACE_HEADER_EMPTY) {
		// If the thing directly to the right is not empty space, we have to make sure that we leave at least enough space for an empty space node
		newSizeLowerLimit -= 8;
	}

	if ((int)newSize >= newSizeLowerLimit) return oldAllocatedSize;

	// Update header and footer for the resized allocation
	*header = newSize | allocationType;
	uint32_t* __restrict__ footer = (uint32_t*)((char*)address + newSize);
	*footer = *header;

	uint32_t emptySpaceStart = (uint32_t)footer + 8;
	uint32_t emptySpaceSize = oldAllocatedSize - newSize - 8;

	markSpaceAsEmpty(emptySpaceStart, emptySpaceSize, false, true);

	return newSize;
}

// Returns how much it was shortened by
uint32_t MemoryRegion::shortenLeft(void* address, uint32_t amountToShorten, uint32_t numBytesToMoveRightIfSuccessful) {

	uint32_t* __restrict__ header = (uint32_t*)((char*)address - 4);
	uint32_t oldAllocatedSize = *header & SPACE_SIZE_MASK;
	uint32_t allocationType = *header & SPACE_TYPE_MASK;

	uint32_t* __restrict__ footer = (uint32_t*)((char*)address + oldAllocatedSize);
	uint32_t newSize = oldAllocatedSize - amountToShorten;

	newSize = getMax(newSize, 4);
	newSize = (newSize + 3) & 0b11111111111111111111111111111100; // Round new size up to 4-byte boundary

	uint32_t* __restrict__ lookLeft =
	    (uint32_t*)((uint32_t)address - 8); // Looking to what's directly left of our old allocated space

	int newSizeLowerLimit = oldAllocatedSize;
	if ((*lookLeft & SPACE_TYPE_MASK) != SPACE_HEADER_EMPTY) {
		// If the thing directly to the left is not empty space, we have to make sure that we leave at least enough space for an empty space node
		newSizeLowerLimit -= 8;
	}

	if ((int)newSize >= newSizeLowerLimit) return 0;

	uint32_t amountShortened = oldAllocatedSize - newSize;

	if (numBytesToMoveRightIfSuccessful) {
		memmove((char*)address + amountShortened, address, numBytesToMoveRightIfSuccessful);
	}

	// Update header and footer for the resized allocation
	header = (uint32_t*)((char*)header + amountShortened);
	*header = newSize | allocationType;
	*footer = *header;

	markSpaceAsEmpty((uint32_t)address, amountShortened - 8, true, false);

	return amountShortened;
}

void MemoryRegion::writeTempHeadersBeforeASteal(uint32_t newStartAddress, uint32_t newSize) {

	uint32_t headerValue = SPACE_HEADER_ALLOCATED | newSize;

	// Because the steal() function is allowed to deallocate or shorten other existing memory, we'd better get
	// our headers in order so it sees that we've claimed what we've claimed so far
	uint32_t* __restrict__ newHeader = (uint32_t*)(newStartAddress - 4);
	uint32_t* __restrict__ footer = (uint32_t*)(newStartAddress + newSize);
	*newHeader = headerValue;
	*footer = headerValue;
}

// Will grab one item of empty or stealable space to the right of the supplied allocation.
// Returns new size, or same size if couldn't extend.
uint32_t MemoryRegion::extendRightAsMuchAsEasilyPossible(void* address) {

	uint32_t* __restrict__ header = (uint32_t*)(static_cast<char*>(address) - 4);
	uint32_t spaceSize = (*header & SPACE_SIZE_MASK);

	uint32_t* __restrict__ lookRight = (uint32_t*)((uint32_t)address + spaceSize + 4);

	uint32_t emptySpaceHereSizeWithoutHeaders = *lookRight & SPACE_SIZE_MASK;
	uint32_t spaceHereAddress = (uint32_t)(lookRight + 1);
	uint32_t spaceType = *lookRight & SPACE_TYPE_MASK;

	if (spaceType == SPACE_HEADER_STEALABLE) {
		Stealable* stealable = (Stealable*)(void*)spaceHereAddress;
		if (!stealable->mayBeStolen(NULL)) goto finished;
		stealable->steal("E446");
		stealable->~Stealable();
	}

	else if (spaceType == SPACE_HEADER_EMPTY) {
		EmptySpaceRecord oldEmptySpace;
		oldEmptySpace.address = spaceHereAddress;
		oldEmptySpace.length = emptySpaceHereSizeWithoutHeaders;
		emptySpaces.deleteAtKeyMultiWord((uint32_t*)&oldEmptySpace);
	}

	else goto finished;

	{
		spaceSize += emptySpaceHereSizeWithoutHeaders + 8;

		uint32_t newHeaderData = spaceSize | SPACE_HEADER_ALLOCATED;

		// Write header
		*header = newHeaderData;

		// Write footer
		uint32_t* __restrict__ footer = (uint32_t*)(static_cast<char*>(address) + spaceSize);
		*footer = newHeaderData;
	}

finished:
	return spaceSize;
}

// Returns new space start address, or NULL if couldn't grab enough memory.
NeighbouringMemoryGrabAttemptResult
MemoryRegion::attemptToGrabNeighbouringMemory(void* originalSpaceAddress, int originalSpaceSize, int minAmountToExtend,
                                              int idealAmountToExtend, void* thingNotToStealFrom,
                                              uint32_t markWithTraversalNo, bool originalSpaceNeedsStealing) {

	NeighbouringMemoryGrabAttemptResult toReturn;

	toReturn.address = (uint32_t)originalSpaceAddress;

	toReturn.amountsExtended[0] = 0;
	toReturn.amountsExtended[1] = 0;

	// Go through twice - once not actually grabbing but just exploring, and then a second time actually grabbing
	for (int actuallyGrabbing = 0; actuallyGrabbing < 2; actuallyGrabbing++) {

		if (actuallyGrabbing && originalSpaceNeedsStealing) {
			((Stealable*)originalSpaceAddress)->steal("E417"); // Jensg still getting.
			((Stealable*)originalSpaceAddress)->~Stealable();
		}

		uint32_t amountOfExtraSpaceFoundSoFar = 0;

		uint32_t* __restrict__ lookRight = (uint32_t*)((uint32_t)originalSpaceAddress + originalSpaceSize + 4);
		uint32_t* __restrict__ lookLeft = (uint32_t*)((uint32_t)originalSpaceAddress - 8);

tryNotStealingFirst:
		// At each point in the exploration, we want to first look both left and right before "stealing", as opposed to just grabbing unused space,
		// in case there's actually more unused space in one of the directions, which would of course be preferable.
		for (int tryingStealingYet = 0; tryingStealingYet < 2; tryingStealingYet++) {

			// If we're going to try stealing, well let's not do that if we've actually found the min amount of memory - to reduce disruption
			if (tryingStealingYet && amountOfExtraSpaceFoundSoFar >= idealAmountToExtend) goto gotEnoughMemory;

			// Look both directions once each
			for (int lookingLeft = 0; lookingLeft < 2; lookingLeft++) {

				uint32_t* __restrict__ lookLeftOrRight = lookingLeft ? lookLeft : lookRight;

				uint32_t emptySpaceHereSizeWithoutHeaders = *lookLeftOrRight & SPACE_SIZE_MASK;

				uint32_t spaceHereAddress =
				    lookingLeft ? (uint32_t)lookLeft - emptySpaceHereSizeWithoutHeaders : (uint32_t)(lookRight + 1);

				Stealable* stealable;

				uint32_t spaceType = *lookLeftOrRight & SPACE_TYPE_MASK;

				switch (spaceType) {
				case SPACE_HEADER_STEALABLE:
					if (!tryingStealingYet) break;
					stealable = (Stealable*)(void*)spaceHereAddress;
					if (!stealable->mayBeStolen(thingNotToStealFrom)) break;
					if (!actuallyGrabbing && markWithTraversalNo) stealable->lastTraversalNo = markWithTraversalNo;
					// No break

				case SPACE_HEADER_EMPTY:
					amountOfExtraSpaceFoundSoFar += emptySpaceHereSizeWithoutHeaders + 8;

					if (lookingLeft) lookLeft = (uint32_t*)((char*)lookLeft - emptySpaceHereSizeWithoutHeaders - 8);
					else lookRight = (uint32_t*)((char*)lookRight + emptySpaceHereSizeWithoutHeaders + 8);

					if (actuallyGrabbing) {

						// If empty space...
						if (spaceType == SPACE_HEADER_EMPTY) {
							EmptySpaceRecord oldEmptySpace;
							oldEmptySpace.address = spaceHereAddress;
							oldEmptySpace.length = emptySpaceHereSizeWithoutHeaders;
							bool success = emptySpaces.deleteAtKeyMultiWord((uint32_t*)&oldEmptySpace);
#if TEST_GENERAL_MEMORY_ALLOCATION
							if (!success) {
								// TODO: actually, this is basically ok - it should be allowed to not find the key, because the empty space might indeed not have a record if there wasn't room to insert one when the empty space was created.
								Uart::println("fail to delete key");
								while (1) {}
							}
#endif
						}

						// Or if stealable space...
						else {

							// Because the steal() function is allowed to deallocate or shorten other existing memory, we'd better get
							// our headers in order so it sees that we've claimed what we've claimed so far
							writeTempHeadersBeforeASteal(toReturn.address, originalSpaceSize
							                                                   + toReturn.amountsExtended[0]
							                                                   + toReturn.amountsExtended[1]);

							stealable->steal("E418"); // Jensg still getting.
							stealable->~Stealable();
						}

						// Can only change these after potentially putting those temp headers in, above
						toReturn.amountsExtended[lookingLeft] += emptySpaceHereSizeWithoutHeaders + 8;
						if (lookingLeft) toReturn.address = spaceHereAddress;
					}

					// Have we got the ideal amount of memory now?
					if (amountOfExtraSpaceFoundSoFar >= idealAmountToExtend) {
						goto gotEnoughMemory;
					}

					// Whether or not actually grabbing, if that was Stealable space we just found, go back and try looking at more, further memory - first prioritizing
					// unused empty space, in case we just stumbled on more
					if (spaceType != SPACE_HEADER_EMPTY) goto tryNotStealingFirst;
				}
			}

			// When we get here, we've just looked both directions.
		}

		// When we get here, we've just tried stealing (and found nothing further in either direction on this particular try)

		// If haven't even got min amount...
		if (amountOfExtraSpaceFoundSoFar < minAmountToExtend) {

			// If we somehow grabbed without finding min amount, then that shouldn't have happened!
#if TEST_GENERAL_MEMORY_ALLOCATION
			if (actuallyGrabbing) {
				Uart::println("grabbed extension without reaching min size"); // This happened recently!
				if (originalSpaceNeedsStealing) Uart::println("during steal");
				else Uart::println("during extend");
				while (1) {}
			}
#endif
			// Anyway yup we didn't find enough memory
			toReturn.address = 0;
			toReturn.longestRunFound = originalSpaceSize + amountOfExtraSpaceFoundSoFar;
			return toReturn;
		}

gotEnoughMemory : {}
		// There's a small chance it will have found a bit less memory the second time through if stealing an allocation resulted in another little bit of memory being freed,
		// that adding onto the discovered amount, and getting us less of a surplus while still reaching the desired (well actually the min) amount
	}

	return toReturn;
}

void MemoryRegion::extend(void* address, uint32_t minAmountToExtend, uint32_t idealAmountToExtend,
                          uint32_t* __restrict__ getAmountExtendedLeft, uint32_t* __restrict__ getAmountExtendedRight,
                          void* thingNotToStealFrom) {

	// Jump to 4-byte boundary
	minAmountToExtend = (minAmountToExtend + 3) & 0b11111111111111111111111111111100;
	idealAmountToExtend = (idealAmountToExtend + 3) & 0b11111111111111111111111111111100;

	uint32_t* header = (uint32_t*)((char*)address - 4);
	uint32_t oldAllocatedSize = (*header & SPACE_SIZE_MASK);

	NeighbouringMemoryGrabAttemptResult grabResult = attemptToGrabNeighbouringMemory(
	    address, oldAllocatedSize, minAmountToExtend, idealAmountToExtend, thingNotToStealFrom);

	// If couldn't get enough new space, fail
	if (!grabResult.address) return;

	// If we found more than we wanted...
	int surplusWeGot = grabResult.amountsExtended[0] + grabResult.amountsExtended[1] - (int)idealAmountToExtend;
	if (surplusWeGot > 8) {

		if (grabResult.amountsExtended[0] > 8) {

			//Uart::println("extend leaving empty space right");

			int amountToCutRightIncludingHeaders = getMax(12, surplusWeGot);
			amountToCutRightIncludingHeaders = getMin(amountToCutRightIncludingHeaders, grabResult.amountsExtended[0]);

			surplusWeGot -= amountToCutRightIncludingHeaders;
			grabResult.amountsExtended[0] -= amountToCutRightIncludingHeaders;

			markSpaceAsEmpty((uint32_t)address + oldAllocatedSize + grabResult.amountsExtended[0] + 8,
			                 amountToCutRightIncludingHeaders - 8, false, false);
		}

		// If we still have more than we wanted...
		if (surplusWeGot > 8) {

			if (grabResult.amountsExtended[1] > 8) {

				//Uart::println("extend leaving empty space left");

				int amountToCutLeftIncludingHeaders = getMax(12, surplusWeGot);
				amountToCutLeftIncludingHeaders =
				    getMin(amountToCutLeftIncludingHeaders, grabResult.amountsExtended[1]);

				surplusWeGot -= amountToCutLeftIncludingHeaders;
				grabResult.amountsExtended[1] -= amountToCutLeftIncludingHeaders;

				markSpaceAsEmpty(grabResult.address, amountToCutLeftIncludingHeaders - 8, false, false);

				grabResult.address += amountToCutLeftIncludingHeaders;
			}
		}
	}

	*getAmountExtendedLeft = grabResult.amountsExtended[1];
	*getAmountExtendedRight = grabResult.amountsExtended[0];

	uint32_t newSize = oldAllocatedSize + grabResult.amountsExtended[0] + grabResult.amountsExtended[1];
	uint32_t newHeaderData = newSize | SPACE_HEADER_ALLOCATED;

	// Write header
	uint32_t* __restrict__ newHeader = (uint32_t*)(grabResult.address - 4);
	*newHeader = newHeaderData;

	// Write footer
	uint32_t* __restrict__ footer = (uint32_t*)(grabResult.address + newSize);
	*footer = newHeaderData;
}

#if TEST_GENERAL_MEMORY_ALLOCATION
uint32_t totalDeallocTime = 0;
int numDeallocTimes = 0;
#endif

void MemoryRegion::dealloc(void* address) {

	//uint16_t startTime = *TCNT[TIMER_SYSTEM_FAST];

	uint32_t* __restrict__ header = (uint32_t*)((uint32_t)address - 4);
	uint32_t spaceSize = (*header & SPACE_SIZE_MASK);

	markSpaceAsEmpty((uint32_t)address, spaceSize);

	/*
	uint16_t endTime = *TCNT[TIMER_SYSTEM_FAST];
	uint16_t timeTaken = endTime - startTime;
	totalDeallocTime += timeTaken;
	numDeallocTimes++;

	Uart::print("average dealloc time: ");
	Uart::println(totalDeallocTime / numDeallocTimes);
	 */
#if TEST_GENERAL_MEMORY_ALLOCATION
	numAllocations--;
#endif
}
