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

#include "memory/memory_region.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "memory/stealable.h"
#include "util/fixedpoint.h"
#include <cstring>

#ifdef DO_AUDIO_LOG
#include "processing/engines/audio_engine.h"
#endif

MemoryRegion::MemoryRegion() : emptySpaces(sizeof(EmptySpaceRecord)) {
}

void MemoryRegion::setup(void* emptySpacesMemory, int32_t emptySpacesMemorySize, uint32_t regionBegin,
                         uint32_t regionEnd, CacheManager* cacheManager) {
	emptySpaces.setStaticMemory(emptySpacesMemory, emptySpacesMemorySize);
	// bit of a hack - the allocations start with a 4 byte type+size header, this ensures the
	// resulting allocations are still aligned to 16 bytes (which should generally be fine for anything?)
	regionBegin = (regionBegin & 0xFFFFFFF0) + 16;

	start = regionBegin;
	// this is actually the location of the footer but that's better anyway
	end = regionEnd - 8;
	uint32_t memorySizeWithoutHeaders = regionEnd - regionBegin - 16;

	*(uint32_t*)regionBegin = SPACE_HEADER_ALLOCATED;
	*(uint32_t*)(regionBegin + 4) = SPACE_HEADER_EMPTY | memorySizeWithoutHeaders;

	*(uint32_t*)(regionEnd - 8) = SPACE_HEADER_EMPTY | memorySizeWithoutHeaders;
	*(uint32_t*)(regionEnd - 4) = SPACE_HEADER_ALLOCATED;

	emptySpaces.insertAtIndex(0);
	EmptySpaceRecord* firstRecord = (EmptySpaceRecord*)emptySpaces.getElementAddress(0);
	firstRecord->length = memorySizeWithoutHeaders;
	firstRecord->address = regionBegin + 8;
	cache_manager_ = cacheManager;
	D_PRINTLN("%x to %x: Memory region %s", start, end, name);
}

uint32_t MemoryRegion::padSize(uint32_t requiredSize) {
	requiredSize += 8; // dirty hack - we need the size with its headers to be aligned so we'll add it here then
	                   // subtract 8 afterwards
	if (requiredSize < minAlign_) {
		requiredSize = minAlign_;
	}
	else {
		int extraSize = 0;
		while (requiredSize > maxAlign_) {
			extraSize += maxAlign_;
			requiredSize -= maxAlign_;
		}
		// if it's not a power of 2 go up to the next power of 2
		if (!((requiredSize & (requiredSize - 1)) == 0)) {
			int magnitude = 32 - clz(requiredSize);
			requiredSize = 1 << magnitude;
		}
		requiredSize += extraSize;
	}
	return requiredSize - 8;
}

bool seenYet = false;

void MemoryRegion::sanityCheck() {
	int32_t count = 0;
	for (int32_t j = 0; j < emptySpaces.getNumElements(); j++) {
		EmptySpaceRecord* emptySpaceRecord = (EmptySpaceRecord*)emptySpaces.getElementAddress(j);
		if (emptySpaceRecord->address == (uint32_t)0xc0080bc) {
			count++;
		}
	}

	if (count > 1) {
		FREEZE_WITH_ERROR("BBBB");
		D_PRINTLN("multiple 0xc0080bc!!!!");
	}
	else if (count == 1) {
		if (!seenYet) {
			seenYet = true;
			D_PRINTLN("seen 0xc0080bc");
		}
	}
}

void MemoryRegion::verifyMemoryNotFree(void* address, uint32_t spaceSize) {
	for (int32_t i = 0; i < emptySpaces.getNumElements(); i++) {
		EmptySpaceRecord* emptySpaceRecord = (EmptySpaceRecord*)emptySpaces.getElementAddress(i);
		if (emptySpaceRecord->address == (uint32_t)address) {
			D_PRINTLN("Exact address free!");
			FREEZE_WITH_ERROR("dddffffd");
		}
		else if (emptySpaceRecord->address <= (uint32_t)address
		         && (emptySpaceRecord->address + emptySpaceRecord->length > (uint32_t)address)) {
			FREEZE_WITH_ERROR("dddd");
			D_PRINTLN("free mem overlap on left!");
		}
		else if ((uint32_t)address <= (uint32_t)emptySpaceRecord->address
		         && ((uint32_t)address + spaceSize > emptySpaceRecord->address)) {
			FREEZE_WITH_ERROR("eeee");
			D_PRINTLN("free mem overlap on right!");
		}
	}
}

// Okay this is me being experimental and trying something you're not supposed to do - using static variables in place
// of stack ones within a function. It seemed to give a slight speed up, but it's probably quite circumstantial, and I
// wouldn't normally do this.
static EmptySpaceRecord emptySpaceToLeft;
static EmptySpaceRecord emptySpaceToRight;
static EmptySpaceRecord* recordToMergeWith;

// Specify the address and size of the actual memory region not including its headers, which this function will write
// and don't have to contain valid data yet. spaceSize can even be 0 or less if you know it's going to get merged.
inline void MemoryRegion::markSpaceAsEmpty(uint32_t address, uint32_t spaceSize, bool mayLookLeft, bool mayLookRight) {
	if ((address < start) || address > end) {
		FREEZE_WITH_ERROR("M998");
		return;
	}
	int32_t biggerRecordSearchFromIndex = 0;
	int32_t insertRangeBegin;

	// Can we merge left?
	if (mayLookLeft) {
		uint32_t* __restrict__ lookLeft = (uint32_t*)(address - 8);
		if ((*lookLeft & SPACE_TYPE_MASK) == SPACE_HEADER_EMPTY) {
			emptySpaceToLeft.length = *lookLeft & SPACE_SIZE_MASK;

			// Expand our empty space region to include this extra space on the left
			spaceSize += emptySpaceToLeft.length + 8;
			address = address - (emptySpaceToLeft.length + 8);
			emptySpaceToLeft.address = address;

			// Set up the default option - that we are going to merge with the left record. This may get overridden
			// below.
			recordToMergeWith = &emptySpaceToLeft;

			// If we're not allowed to also look right, or there's no unused space there, we want to just go directly to
			// replacing this old record
			if (!mayLookRight) {
				goto goingToReplaceOldRecord;
			}
			uint32_t* __restrict__ lookRight = (uint32_t*)(address + spaceSize + 4);
			if ((*lookRight & SPACE_TYPE_MASK) != SPACE_HEADER_EMPTY) {
				goto goingToReplaceOldRecord;
			}

			// If we are still here, we are going to merge right as well as left, so gather a little bit more info
			emptySpaceToRight.length = *lookRight & SPACE_SIZE_MASK;
			spaceSize += emptySpaceToRight.length + 8;
			emptySpaceToRight.address = (uint32_t)lookRight + 4;
			EmptySpaceRecord* recordToDelete = &emptySpaceToRight; // The default, which we may override below.

			// If the "right" space is bigger, delete the "left" one's record
			// address here is same as, and best thought of as, emptySpaceToLeft.address.
			// Hang on, wouldn't the space to the right always have a higher address?
			if (emptySpaceToRight.length > emptySpaceToLeft.length
			    || (emptySpaceToRight.length == emptySpaceToLeft.length && emptySpaceToRight.address > address)) {
				recordToMergeWith = &emptySpaceToRight;
				recordToDelete = &emptySpaceToLeft;
			}
			// Otherwise, do the opposite - delete the "right" space's record. That default was set back up above.

			int32_t nextIndex;
			biggerRecordSearchFromIndex = emptySpaces.searchMultiWordExact((uint32_t*)recordToDelete, &nextIndex);

			// It might not have been found if array got full so there was no record for this empty space.
			if (biggerRecordSearchFromIndex == -1) {
				biggerRecordSearchFromIndex = nextIndex;
			}
			else {
				// TODO: Ideally we like to combine this deletion with the below reorganisation of records. But this is
				// actually very complicated, and maybe not even worth it.
				emptySpaces.deleteAtIndex(biggerRecordSearchFromIndex);
			}
			goto goingToReplaceOldRecord;
		}
	}

	// Even if we didn't merge left, we may still want to merge right - and in this case, where that is the only merge
	// we are doing, it's much simpler.
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
		int32_t i = emptySpaces.searchMultiWordExact((uint32_t*)&newRecord);
		if (i != -1) {
			FREEZE_WITH_ERROR("M123");
		}
		i = emptySpaces.insertAtKeyMultiWord((uint32_t*)&newRecord, insertRangeBegin);
#if ALPHA_OR_BETA_VERSION
		if (i == -1) { // Array might have gotten full. This has to be coped with. Perhaps in a perfect world we should
			           // opt to throw away the smallest empty space to make space for this one if this one is bigger?
			D_PRINTLN("Lost track of empty space in region:  %s", name);
		}
#endif
	}

	if (false) {

goingToReplaceOldRecord:
		int32_t i = emptySpaces.searchMultiWordExact((uint32_t*)recordToMergeWith, &insertRangeBegin,
		                                             biggerRecordSearchFromIndex);
		if (i == -1) { // The record might not exist because there wasn't room to insert it when the empty space was
			           // created.
#if ALPHA_OR_BETA_VERSION
			D_PRINTLN("Found orphaned empty space in region:  %s", name);
#endif
			goto justInsertRecord;
		}

		// If there is a "bigger" record, to the right in the array...
		if (i < emptySpaces.getNumElements() - 1) {
			EmptySpaceRecord* nextBiggerRecord = (EmptySpaceRecord*)emptySpaces.getElementAddress(i + 1);
			int32_t howMuchBigger = nextBiggerRecord->length - spaceSize;

			// If that next "bigger" record to the right in the array is actually smaller than our new space, we'll have
			// to rearrange some elements.
			if (howMuchBigger <= 0 && !(!howMuchBigger && nextBiggerRecord->address > address)) {

				EmptySpaceRecord newRecordPreview;
				newRecordPreview.length = spaceSize;
				newRecordPreview.address = address;

				int32_t insertBefore =
				    emptySpaces.searchMultiWord((uint32_t*)&newRecordPreview, GREATER_OR_EQUAL, i + 2);
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
	emptySpaces.testSequentiality("M005");
}

void* MemoryRegion::alloc(uint32_t requiredSize, bool makeStealable, void* thingNotToStealFrom) {
	requiredSize = padSize(requiredSize);
	bool large = requiredSize > pivot_;
	// set a minimum size	requiredSize = padSize(requiredSize);
	int32_t allocatedSize;
	uint32_t allocatedAddress;
	int32_t i;

	if (!emptySpaces.getNumElements()) {
		goto noEmptySpace;
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
		if (allocatedAddress < start || allocatedAddress > end) {
			// trying to allocate outside our region
			D_PRINTLN("Memory region out of bounds at %x, start is %x and end is %x", allocatedAddress, start, end);
			FREEZE_WITH_ERROR("M002");
		}
		int32_t extraSpaceSizeWithoutItsHeaders = allocatedSize - requiredSize - 8;
		if (extraSpaceSizeWithoutItsHeaders < -8) {
			FREEZE_WITH_ERROR("M003");
		}
		else {
			if (extraSpaceSizeWithoutItsHeaders <= minAlign_) {
				emptySpaces.deleteAtIndex(i);
			}
			else {

				allocatedSize = requiredSize;
				uint32_t extraSpaceAddress;
				// basically the idea here is that small things get allocated at the end of
				// the space, and large things are at the beginning
				// setting pivot to 0 restores original behaviour
				// This reduces fragmentation and avoids chains of steals
				if (!large) {
					extraSpaceAddress = allocatedAddress;
					allocatedAddress = extraSpaceAddress + extraSpaceSizeWithoutItsHeaders + 8;
				}
				else {
					extraSpaceAddress = allocatedAddress + allocatedSize + 8;
				}

				uint32_t* __restrict__ header = (uint32_t*)((uint32_t)extraSpaceAddress - 4);
				uint32_t* __restrict__ footer =
				    (uint32_t*)((uint32_t)extraSpaceAddress + extraSpaceSizeWithoutItsHeaders);

				// Update headers and footers
				uint32_t headerData = SPACE_HEADER_EMPTY | extraSpaceSizeWithoutItsHeaders;
				*header = headerData;
				*footer = headerData;

				// Hopefully we can just update the same empty space record.
				// We definitely can if it was the leftmost record (smallest empty space).
				if (!i) {
					goto justUpdateRecord;
				}

				{
					// Or even if it wasn't the leftmost record, we might still be able to just simply update - if our
					// new value is still bigger than the record to the left.
					EmptySpaceRecord* nextSmallerRecord = (EmptySpaceRecord*)emptySpaces.getElementAddress(i - 1);
					int32_t howMuchBiggerStill = extraSpaceSizeWithoutItsHeaders - nextSmallerRecord->length;
					if (howMuchBiggerStill > 0
					    || (!howMuchBiggerStill && extraSpaceAddress > nextSmallerRecord->address)) {
						goto justUpdateRecord;
					}

					// Okay, if we're here, we have to rearrange some records.
					// Find the best empty space
					EmptySpaceRecord searchThing;
					searchThing.length = extraSpaceSizeWithoutItsHeaders;
					searchThing.address = extraSpaceAddress;
					int32_t insertAt = emptySpaces.searchMultiWord((uint32_t*)&searchThing, GREATER_OR_EQUAL, 0, i);

					emptySpaces.moveElementsRight(insertAt, i, 1);

					emptySpaceRecord = (EmptySpaceRecord*)emptySpaces.getElementAddress(insertAt);
				}
justUpdateRecord:
				emptySpaceRecord->length = extraSpaceSizeWithoutItsHeaders;
				emptySpaceRecord->address = extraSpaceAddress;
			}
		}
	}

	// Or if no empty space big enough, try stealing some memory
	else {
noEmptySpace:
		if (cache_manager_) {
			allocatedAddress = cache_manager_->ReclaimMemory(*this, requiredSize, thingNotToStealFrom, &allocatedSize);
		}
		if (!allocatedAddress) {
#if ALPHA_OR_BETA_VERSION
			if (name) {
				const uint32_t msgBufferLen = 32;
				char msgBuffer[msgBufferLen] = {0};
				strncpy(&msgBuffer[strlen(msgBuffer)], "-> FULL ", msgBufferLen - strlen(msgBuffer));

				strncpy(&msgBuffer[strlen(msgBuffer)], name, msgBufferLen - strlen(name));

				D_PRINTLN(msgBuffer);

#if !defined(NDEBUG)
				display->displayPopup(msgBuffer);
#endif
			}
#endif

			return NULL;
		}

		// D_PRINTLN("Reclaimed");

		// See if there was some extra space left over
		int32_t extraSpaceSizeWithoutItsHeaders = allocatedSize - requiredSize - 8;
		if (requiredSize && extraSpaceSizeWithoutItsHeaders > minAlign_) {
			allocatedSize = requiredSize;
			markSpaceAsEmpty(allocatedAddress + allocatedSize + 8, extraSpaceSizeWithoutItsHeaders, false, false);
		}
		else if (extraSpaceSizeWithoutItsHeaders < -8) {
			FREEZE_WITH_ERROR("M004");
		}
	}

	uint32_t* __restrict__ header = (uint32_t*)(allocatedAddress - 4);
	uint32_t* __restrict__ footer = (uint32_t*)(allocatedAddress + allocatedSize);

	uint32_t headerData = (makeStealable ? SPACE_HEADER_STEALABLE : SPACE_HEADER_ALLOCATED) | allocatedSize;
	*header = headerData;
	*footer = headerData;

	numAllocations_++;

#if ALPHA_OR_BETA_VERSION
	if (allocatedAddress < start || allocatedAddress > end) {
		// trying to allocate outside our region
		D_PRINTLN("Memory region out of bounds at %x, start is %x and end is %x", allocatedAddress, start, end);
		FREEZE_WITH_ERROR("M002");
	}
#endif
	return (void*)allocatedAddress;
}

// Returns new size
uint32_t MemoryRegion::shortenRight(void* address, uint32_t newSize) {
	newSize = padSize(newSize);
	uint32_t* __restrict__ header = (uint32_t*)((char*)address - 4);
	uint32_t oldAllocatedSize = *header & SPACE_SIZE_MASK;
	uint32_t allocationType = *header & SPACE_TYPE_MASK;

	// Looking to what's directly right of our old allocated space
	uint32_t* __restrict__ lookRight = (uint32_t*)((uint32_t)address + oldAllocatedSize + 4);

	int32_t newSizeLowerLimit = oldAllocatedSize;
	if ((*lookRight & SPACE_TYPE_MASK) != SPACE_HEADER_EMPTY) {
		// If the thing directly to the right is not empty space, we have to make sure that we leave at least enough
		// space for an empty space node
		newSizeLowerLimit -= 8;
	}

	if ((int32_t)newSize >= newSizeLowerLimit) {
		return oldAllocatedSize;
	}

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

	newSize = padSize(newSize);

	// Looking to what's directly left of our old allocated space
	uint32_t* __restrict__ lookLeft = (uint32_t*)((uint32_t)address - 8);

	int32_t newSizeLowerLimit = oldAllocatedSize;
	if ((*lookLeft & SPACE_TYPE_MASK) != SPACE_HEADER_EMPTY) {
		// If the thing directly to the left is not empty space, we have to make sure that we leave at least enough
		// space for an empty space node
		newSizeLowerLimit -= 8;
	}

	if ((int32_t)newSize >= newSizeLowerLimit) {
		return 0;
	}

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
	uint32_t currentSpaceType = *header & SPACE_TYPE_MASK;

	uint32_t* __restrict__ lookRight = (uint32_t*)((uint32_t)address + spaceSize + 4);

	uint32_t emptySpaceHereSizeWithoutHeaders = *lookRight & SPACE_SIZE_MASK;
	uint32_t spaceHereAddress = (uint32_t)(lookRight + 1);
	uint32_t spaceType = *lookRight & SPACE_TYPE_MASK;

	if (spaceType == SPACE_HEADER_STEALABLE) {
		Stealable* stealable = (Stealable*)(void*)spaceHereAddress;
		if (!stealable->mayBeStolen(NULL)) {
			goto finished;
		}
		stealable->steal("E446");
		stealable->~Stealable();
	}

	else if (spaceType == SPACE_HEADER_EMPTY) {
		EmptySpaceRecord oldEmptySpace;
		oldEmptySpace.address = spaceHereAddress;
		oldEmptySpace.length = emptySpaceHereSizeWithoutHeaders;
		emptySpaces.deleteAtKeyMultiWord((uint32_t*)&oldEmptySpace);
	}

	else {
		goto finished;
	}

	{
		spaceSize += emptySpaceHereSizeWithoutHeaders + 8;

		uint32_t newHeaderData = spaceSize | currentSpaceType;

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
NeighbouringMemoryGrabAttemptResult MemoryRegion::attemptToGrabNeighbouringMemory(
    void* originalSpaceAddress, int32_t originalSpaceSize, int32_t minAmountToExtend, int32_t idealAmountToExtend,
    void* thingNotToStealFrom, uint32_t markWithTraversalNo, bool originalSpaceNeedsStealing) {

	NeighbouringMemoryGrabAttemptResult toReturn;

	toReturn.address = (uint32_t)originalSpaceAddress;

	toReturn.amountsExtended[0] = 0;
	toReturn.amountsExtended[1] = 0;

	// Go through twice - once not actually grabbing but just exploring, and then a second time actually grabbing
	for (int32_t actuallyGrabbing = 0; actuallyGrabbing < 2; actuallyGrabbing++) {

		if (actuallyGrabbing && originalSpaceNeedsStealing) {
			((Stealable*)originalSpaceAddress)->steal("E417"); // Jensg still getting.
			((Stealable*)originalSpaceAddress)->~Stealable();
		}

		uint32_t amountOfExtraSpaceFoundSoFar = 0;

		uint32_t* __restrict__ lookRight = (uint32_t*)((uint32_t)originalSpaceAddress + originalSpaceSize + 4);
		uint32_t* __restrict__ lookLeft = (uint32_t*)((uint32_t)originalSpaceAddress - 8);

tryNotStealingFirst:
		// At each point in the exploration, we want to first look both left and right before "stealing", as opposed to
		// just grabbing unused space, in case there's actually more unused space in one of the directions, which would
		// of course be preferable.
		for (int32_t tryingStealingYet = 0; tryingStealingYet < 2; tryingStealingYet++) {

			// If we're going to try stealing, well let's not do that if we've actually found the min amount of memory -
			// to reduce disruption
			if (tryingStealingYet && amountOfExtraSpaceFoundSoFar >= idealAmountToExtend) {
				goto gotEnoughMemory;
			}

			// Look both directions once each
			for (int32_t lookingLeft = 0; lookingLeft < 2; lookingLeft++) {

				uint32_t* __restrict__ lookLeftOrRight = lookingLeft ? lookLeft : lookRight;

				uint32_t emptySpaceHereSizeWithoutHeaders = *lookLeftOrRight & SPACE_SIZE_MASK;

				uint32_t spaceHereAddress =
				    lookingLeft ? (uint32_t)lookLeft - emptySpaceHereSizeWithoutHeaders : (uint32_t)(lookRight + 1);

				Stealable* stealable;

				uint32_t spaceType = *lookLeftOrRight & SPACE_TYPE_MASK;

				switch (spaceType) {
				case SPACE_HEADER_STEALABLE:
					if (!tryingStealingYet) {
						break;
					}
					stealable = (Stealable*)(void*)spaceHereAddress;
					if (!stealable->mayBeStolen(thingNotToStealFrom)) {
#ifdef DO_AUDIO_LOG
						AudioEngine::logAction("found a stealable with a reason");
#endif
						break;
					}
					if (!actuallyGrabbing && markWithTraversalNo) {
						stealable->lastTraversalNo = markWithTraversalNo;
					}
					// No break

				case SPACE_HEADER_EMPTY:
					amountOfExtraSpaceFoundSoFar += emptySpaceHereSizeWithoutHeaders + 8;

					if (lookingLeft) {
						lookLeft = (uint32_t*)((char*)lookLeft - emptySpaceHereSizeWithoutHeaders - 8);
					}
					else {
						lookRight = (uint32_t*)((char*)lookRight + emptySpaceHereSizeWithoutHeaders + 8);
					}

					if (actuallyGrabbing) {

						// If empty space...
						if (spaceType == SPACE_HEADER_EMPTY) {
							EmptySpaceRecord oldEmptySpace;
							oldEmptySpace.address = spaceHereAddress;
							oldEmptySpace.length = emptySpaceHereSizeWithoutHeaders;
							bool success = emptySpaces.deleteAtKeyMultiWord((uint32_t*)&oldEmptySpace);
#if TEST_GENERAL_MEMORY_ALLOCATION
							if (!success) {
								// TODO: actually, this is basically ok - it should be allowed to not find the key,
								// because the empty space might indeed not have a record if there wasn't room to insert
								// one when the empty space was created.
								D_PRINTLN("fail to delete key");
								while (1) {}
							}
#endif
						}

						// Or if stealable space...
						else {

							// Because the steal() function is allowed to deallocate or shorten other existing memory,
							// we'd better get our headers in order so it sees that we've claimed what we've claimed so
							// far
							writeTempHeadersBeforeASteal(toReturn.address, originalSpaceSize
							                                                   + toReturn.amountsExtended[0]
							                                                   + toReturn.amountsExtended[1]);

							stealable->steal("E418"); // Jensg still getting.
							stealable->~Stealable();
						}

						// Can only change these after potentially putting those temp headers in, above
						toReturn.amountsExtended[lookingLeft] += emptySpaceHereSizeWithoutHeaders + 8;
						if (lookingLeft) {
							toReturn.address = spaceHereAddress;
						}
					}

					// Have we got the ideal amount of memory now?
					if (amountOfExtraSpaceFoundSoFar >= idealAmountToExtend) {
						goto gotEnoughMemory;
					}

					// Whether or not actually grabbing, if that was Stealable space we just found, go back and try
					// looking at more, further memory - first prioritizing unused empty space, in case we just stumbled
					// on more
					// No - if we're not grabbing this can cause us to exit early without stealing all intermediate
					// memory
					if (tryingStealingYet) {
#ifdef DO_AUDIO_LOG
						AudioEngine::logAction("found some space and looking for more");
#endif
						goto tryNotStealingFirst;
					}
				case SPACE_HEADER_ALLOCATED:
					break;
				default:
					D_PRINTLN("no match !!!!!!");
				}
			}

			// When we get here, we've just looked both directions.
		}

		// When we get here, we've just tried stealing (and found nothing further in either direction on this particular
		// try)

		// If haven't even got min amount...
		if (amountOfExtraSpaceFoundSoFar < minAmountToExtend) {

			// If we somehow grabbed without finding min amount, then that shouldn't have happened!
#if TEST_GENERAL_MEMORY_ALLOCATION
			if (actuallyGrabbing) {
				D_PRINTLN("grabbed extension without reaching min size"); // This happened recently!
				if (originalSpaceNeedsStealing)
					D_PRINTLN("during steal");
				else
					D_PRINTLN("during extend");
				while (1) {}
			}
#endif
			// Anyway yup we didn't find enough memory
			toReturn.address = 0;
			toReturn.longestRunFound = originalSpaceSize + amountOfExtraSpaceFoundSoFar;
			return toReturn;
		}

gotEnoughMemory: {}
		// There's a small chance it will have found a bit less memory the second time through if stealing an allocation
		// resulted in another little bit of memory being freed, that adding onto the discovered amount, and getting us
		// less of a surplus while still reaching the desired (well actually the min) amount
	}

	return toReturn;
}

void MemoryRegion::extend(void* address, uint32_t minAmountToExtend, uint32_t idealAmountToExtend,
                          uint32_t* __restrict__ getAmountExtendedLeft, uint32_t* __restrict__ getAmountExtendedRight,
                          void* thingNotToStealFrom) {
	minAmountToExtend = padSize(minAmountToExtend);
	idealAmountToExtend = padSize(idealAmountToExtend);

	uint32_t* header = (uint32_t*)((char*)address - 4);
	uint32_t oldAllocatedSize = (*header & SPACE_SIZE_MASK);
	uint32_t oldHeader = (*header & SPACE_TYPE_MASK);

	NeighbouringMemoryGrabAttemptResult grabResult = attemptToGrabNeighbouringMemory(
	    address, oldAllocatedSize, minAmountToExtend, idealAmountToExtend, thingNotToStealFrom);

	// If couldn't get enough new space, fail
	if (!grabResult.address) {
		return;
	}

	// If we found more than we wanted...
	int32_t surplusWeGot = grabResult.amountsExtended[0] + grabResult.amountsExtended[1] - (int32_t)idealAmountToExtend;
	if (surplusWeGot > 8) {

		if (grabResult.amountsExtended[0] > 8) {

			// D_PRINTLN("extend leaving empty space right");

			int32_t amountToCutRightIncludingHeaders = std::max(12_i32, surplusWeGot);
			amountToCutRightIncludingHeaders =
			    std::min(amountToCutRightIncludingHeaders, grabResult.amountsExtended[0]);

			surplusWeGot -= amountToCutRightIncludingHeaders;
			grabResult.amountsExtended[0] -= amountToCutRightIncludingHeaders;

			markSpaceAsEmpty((uint32_t)address + oldAllocatedSize + grabResult.amountsExtended[0] + 8,
			                 amountToCutRightIncludingHeaders - 8, false, false);
		}

		// If we still have more than we wanted...
		if (surplusWeGot > 8) {

			if (grabResult.amountsExtended[1] > 8) {

				// D_PRINTLN("extend leaving empty space left");

				int32_t amountToCutLeftIncludingHeaders = std::max(12_i32, surplusWeGot);
				amountToCutLeftIncludingHeaders =
				    std::min(amountToCutLeftIncludingHeaders, grabResult.amountsExtended[1]);

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
	uint32_t newHeaderData = newSize | oldHeader;

	// Write header
	uint32_t* __restrict__ newHeader = (uint32_t*)(grabResult.address - 4);
	*newHeader = newHeaderData;

	// Write footer
	uint32_t* __restrict__ footer = (uint32_t*)(grabResult.address + newSize);
	*footer = newHeaderData;
}

#if TEST_GENERAL_MEMORY_ALLOCATION
uint32_t totalDeallocTime = 0;
int32_t numDeallocTimes = 0;
#endif

void MemoryRegion::dealloc(void* address) {

	// uint16_t startTime = *TCNT[TIMER_SYSTEM_FAST];

	uint32_t* __restrict__ header = (uint32_t*)((uint32_t)address - 4);

	uint32_t spaceSize = (*header & SPACE_SIZE_MASK);

#if ALPHA_OR_BETA_VERSION
	if ((uint32_t)address < start || (uint32_t)address > end) {
		// deallocating outside our region
		FREEZE_WITH_ERROR("M001");
	}
	if ((*header & SPACE_TYPE_MASK) == SPACE_HEADER_EMPTY) {
		// double free
		FREEZE_WITH_ERROR("M000");
	}
#endif

	markSpaceAsEmpty((uint32_t)address, spaceSize);

	/*
	uint16_t endTime = *TCNT[TIMER_SYSTEM_FAST];
	uint16_t timeTaken = endTime - startTime;
	totalDeallocTime += timeTaken;
	numDeallocTimes++;

	D_PRINTLN("average dealloc time:  %d", totalDeallocTime / numDeallocTimes);
	 */
#if TEST_GENERAL_MEMORY_ALLOCATION
	numAllocations--;
#endif
}
