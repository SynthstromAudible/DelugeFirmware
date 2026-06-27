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
#include "memory/alloc_metadata.h"
#include "memory/general_memory_allocator.h"
#include "memory/heap_poison.h"
#include "memory/stealable.h"
#include "util/fixedpoint.h"
#include <cstring>

#ifdef DO_AUDIO_LOG
#include "processing/engines/audio_engine.h"
#endif

#if MEM_GUARD
namespace {
// Fills a just-freed block's body with the free poison. Block sizes are 4-aligned, so a word-at-a-time fill covers the
// whole body; any trailing bytes are below the footer and irrelevant. Cost is the block size - normal frees are small;
// the occasional large free (e.g. an audio Cluster) is a sub-millisecond memset, which is fine.
void memGuardPoisonBody(uint32_t address, uint32_t size) {
	uint32_t* p = (uint32_t*)address;
	for (uint32_t i = 0, words = size >> 2; i < words; i++) {
		p[i] = MEM_GUARD_FREE_POISON;
	}
}

// Validates that an empty (or about-to-be-reused) block body is clean: every word is either the free poison (block was
// freed at some point) or zero (never allocated - startup clears SDRAM/bss). A use-after-free or stray write that
// deposited anything else is caught here. Returns the first bad word address, or 0 if clean.
uint32_t memGuardFindDirtyWord(uint32_t address, uint32_t size) {
	uint32_t* p = (uint32_t*)address;
	for (uint32_t i = 0, words = size >> 2; i < words; i++) {
		if (p[i] != MEM_GUARD_FREE_POISON && p[i] != 0u) {
			return (uint32_t)&p[i];
		}
	}
	return 0;
}

// Logs the allocation call-site for a block, if we have one. `label` distinguishes e.g. the corrupted block from the
// neighbour that likely overran into it. The printed call-site resolves to a source line via:
//   arm-none-eabi-addr2line -e build/Debug/deluge.elf <callsite>
void memGuardReportOwner(char const* label, uint32_t address) {
	mem_guard::AllocInfo info;
	if (mem_guard::lookupAlloc((void*)address, &info)) {
		D_PRINTLN("  %s block %x allocated from %x (requested %x bytes)", label, address, info.callsite,
		          info.requestedSize);
	}
	else {
		D_PRINTLN("  %s block %x: no allocation record (free/stealable/untracked)", label, address);
	}
}
} // namespace
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

	// The whole region starts out as one free block; poison its body so accesses to not-yet-allocated space are caught
	// too (alloc() unpoisons each block as it's carved out).
	DELUGE_HEAP_POISON((void*)(regionBegin + 8), memorySizeWithoutHeaders);

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

#if MEM_GUARD
bool MemoryRegion::checkIntegrity(char const* reason) {
	// Blocks are laid out contiguously and boundary-tagged: a block at user-address A has a 4-byte header at (A - 4)
	// and a matching 4-byte footer at (A + size), both holding the same type+size word. The next block's user-address
	// is therefore (A + size + 8). The first block sits at (start + 8) - the word at `start` itself is a permanent left
	// guard - and the final block's footer lands exactly on `end`, so the walk should finish at (end + 8). A buffer
	// overrun into a neighbour corrupts that neighbour's header/footer, so a header != footer mismatch (or a
	// nonsensical size/type) pinpoints the damage.
	uint32_t address = start + 8;
	uint32_t prevAddress = 0; // the block immediately to the left - the usual culprit when a header gets clobbered
	while (address <= end) {
		uint32_t* header = (uint32_t*)(address - 4);
		uint32_t headerData = *header;
		uint32_t spaceType = headerData & SPACE_TYPE_MASK;
		uint32_t spaceSize = headerData & SPACE_SIZE_MASK;

		// Type must be one of the three legal values.
		if (spaceType != SPACE_HEADER_EMPTY && spaceType != SPACE_HEADER_STEALABLE
		    && spaceType != SPACE_HEADER_ALLOCATED) {
			D_PRINTLN("MEM_GUARD bad block type %x at %x in region %s (%s)", spaceType, address, name, reason);
			if (prevAddress) {
				memGuardReportOwner("overrun candidate (left neighbour)", prevAddress);
			}
			FREEZE_WITH_ERROR("MG02");
			return false;
		}

		// Size must be sane: nonzero, 4-aligned, and not running our footer past the end of the region.
		if (spaceSize == 0 || (spaceSize & 3u) != 0 || address + spaceSize > end) {
			D_PRINTLN("MEM_GUARD bad block size %x at %x in region %s (%s)", spaceSize, address, name, reason);
			if (prevAddress) {
				memGuardReportOwner("overrun candidate (left neighbour)", prevAddress);
			}
			FREEZE_WITH_ERROR("MG02");
			return false;
		}

		// Header and footer must agree - the core boundary-tag corruption check.
		uint32_t* footer = (uint32_t*)(address + spaceSize);
		if (*footer != headerData) {
			D_PRINTLN("MEM_GUARD header/footer mismatch at %x (header %x footer %x) region %s (%s)", address,
			          headerData, *footer, name, reason);
			memGuardReportOwner("corrupted", address);
			memGuardReportOwner("right neighbour (overran into footer)", address + spaceSize + 8);
			FREEZE_WITH_ERROR("MG02");
			return false;
		}

		// An EMPTY block's body must still be clean poison/zero. A stray write into freed memory (use-after-free, or an
		// overrun that landed in a free block's body rather than its tags) is caught here.
		if (spaceType == SPACE_HEADER_EMPTY) {
			uint32_t dirty = memGuardFindDirtyWord(address, spaceSize);
			if (dirty) {
				D_PRINTLN("MEM_GUARD dirty free block: word %x (=%x) in empty block %x size %x region %s (%s)", dirty,
				          *(uint32_t*)dirty, address, spaceSize, name, reason);
				if (prevAddress) {
					memGuardReportOwner("overrun candidate (left neighbour)", prevAddress);
				}
				FREEZE_WITH_ERROR("MG03");
				return false;
			}
		}

		// Step to the next block: skip this block's body, its footer (4) and the next block's header (4).
		prevAddress = address;
		address += spaceSize + 8;
	}

	// The walk must finish exactly at (end + 8): the last block's footer is at `end`, so its successor address is
	// end + 8. Anything else means a size somewhere was wrong in a way that still kept individual tags self-consistent.
	if (address != end + 8) {
		D_PRINTLN("MEM_GUARD block walk misaligned in region %s: landed at %x expected %x (%s)", name, address, end + 8,
		          reason);
		FREEZE_WITH_ERROR("MG02");
		return false;
	}

	return true;
}
#endif

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

	// The 4-byte boundary tags are allocator-owned metadata that we read/write constantly to navigate the heap, so they
	// must always be addressable. When this block was formed by merging a freed neighbour, a tag can sit inside that
	// neighbour's poisoned body - unpoison the tag words before writing them (the body itself is (re)poisoned by the
	// caller after this returns). Compiles to nothing on the device / without a sanitizer.
	DELUGE_HEAP_UNPOISON(header, 4);
	DELUGE_HEAP_UNPOISON(footer, 4);

	uint32_t headerData = SPACE_HEADER_EMPTY | spaceSize;
	*header = headerData;
	*footer = headerData;

#if MEM_GUARD
	// Poison the whole freed body (now at its final, fully-merged extent). Any later read shows the poison pattern, and
	// any later write is caught by verify-on-realloc or the periodic walk. Overwrites the interior tags of any blocks
	// just merged in, which is correct - they're now part of this one empty block's body.
	memGuardPoisonBody(address, spaceSize);
#endif

	emptySpaces.testSequentiality("M005");
}

void* MemoryRegion::alloc(uint32_t requiredSize, bool makeStealable, void* thingNotToStealFrom) {
	requiredSize = padSize(requiredSize);
	bool large = requiredSize > pivot_;
	// set a minimum size	requiredSize = padSize(requiredSize);
	int32_t allocatedSize = 0;
	uint32_t allocatedAddress = 0;
	int32_t i;
#if MEM_GUARD
	// Whether this allocation came from previously-empty space (poison/zero, so safe to verify) rather than from
	// stealing a live Stealable (whose memory is not poisoned). Declared before any goto so the jumps are well-formed.
	bool fromEmptySpace = false;
#endif

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
#if MEM_GUARD
		fromEmptySpace = true;
#endif
		EmptySpaceRecord* emptySpaceRecord = (EmptySpaceRecord*)emptySpaces.getElementAddress(i);

		allocatedSize = emptySpaceRecord->length;
		allocatedAddress = emptySpaceRecord->address;

		// Unpoison the whole free block before carving it: the split logic below writes splinter boundary tags inside
		// this body, and the returned user block is part of it. Any left-over splinter stays accessible (it's free
		// space, re-poisoned only when a user block sitting in it is later freed) - a small gap, but it keeps the
		// allocator's own interior tag writes from tripping the sanitizer.
		DELUGE_HEAP_UNPOISON((void*)allocatedAddress, allocatedSize);

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
		if (allocatedAddress == 0u) {
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

			return nullptr;
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

#if MEM_GUARD
	// Verify-on-realloc: a block carved from previously-empty space must still hold the free poison (or zero). Anything
	// else means something wrote to this memory while it was free - a use-after-free. The body is still untouched here:
	// the header/footer writes above sit outside [allocatedAddress, allocatedAddress + allocatedSize), and any leftover
	// splinter created by the split was placed beyond those bounds too.
	if (fromEmptySpace) {
		uint32_t dirty = memGuardFindDirtyWord(allocatedAddress, allocatedSize);
		if (dirty) {
			D_PRINTLN("MEM_GUARD use-after-free: dirty word %x (=%x) in block %x size %x region %s", dirty,
			          *(uint32_t*)dirty, allocatedAddress, (uint32_t)allocatedSize, name);
			memGuardReportOwner("most-recent owner of reused address", allocatedAddress);
			FREEZE_WITH_ERROR("MG03");
		}
	}
#endif
	// Hand a clean (accessible) block to the caller; it was poisoned while it sat free (see dealloc / setup).
	DELUGE_HEAP_UNPOISON((void*)allocatedAddress, allocatedSize);
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
		if (!stealable->mayBeStolen(nullptr)) {
			goto finished;
		}
		stealable->steal("E446");
		stealable->~Stealable();
		// The stolen object has been destroyed; poison its body so a dangling reference to the stolen Stealable is
		// caught. The memory is unpoisoned again when it's legitimately handed back out (alloc's return / the carve
		// unpoison), so this only flags use of the stolen object before that point.
		DELUGE_HEAP_POISON((void*)spaceHereAddress, emptySpaceHereSizeWithoutHeaders);
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
			// Stolen object destroyed - poison until the grabbed region is handed back out (see note at the E446 site).
			DELUGE_HEAP_POISON(originalSpaceAddress, originalSpaceSize);
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
							// Stolen object destroyed - poison until handed back out (see note at the E446 site).
							DELUGE_HEAP_POISON((void*)spaceHereAddress, emptySpaceHereSizeWithoutHeaders);
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

#if MEM_GUARD
	// Boundary-tag check: if this block was overrun (or its neighbour overran into our footer), the footer will no
	// longer match the header. Catches the corruption at free time, pinpointing the offending block.
	uint32_t* footer = (uint32_t*)((uint32_t)address + spaceSize);
	if (*footer != *header) {
		D_PRINTLN("MEM_GUARD header/footer mismatch on free at %x (header %x footer %x) region %s", (uint32_t)address,
		          *header, *footer, name);
		memGuardReportOwner("corrupted", (uint32_t)address);
		memGuardReportOwner("right neighbour (overran into footer)", (uint32_t)address + spaceSize + 8);
		FREEZE_WITH_ERROR("MG01");
	}
#endif

	markSpaceAsEmpty((uint32_t)address, spaceSize);

	// The block is now free. Poison its body so any later read/write of the freed object is caught. markSpaceAsEmpty
	// only touches block headers/footers and the external empty-space records, never the body (the MEM_GUARD body fill
	// is compiled out under a sanitizer), so nothing in the allocator trips on this.
	DELUGE_HEAP_POISON(address, spaceSize);

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
