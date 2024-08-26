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

#pragma once

#include "memory/cache_manager.h"
#include "util/container/array/ordered_resizeable_array_with_multi_word_key.h"

#include <util/exceptions.h>

struct EmptySpaceRecord {
	uint32_t length;
	uint32_t address;
};

struct NeighbouringMemoryGrabAttemptResult {
	uint32_t address; // 0 means didn't grab / not found.
	int32_t amountsExtended[2];
	uint32_t longestRunFound; // Only valid if didn't return some space.
};
#define SPACE_HEADER_EMPTY 0
#define SPACE_HEADER_STEALABLE 0x40000000
#define SPACE_HEADER_ALLOCATED 0x80000000

#define SPACE_TYPE_MASK 0xC0000000u
#define SPACE_SIZE_MASK 0x3FFFFFFFu
constexpr size_t max_align_big = 1 << 12;
constexpr size_t min_align_big = 64;
constexpr size_t pivot_big = 512;
class MemoryRegion {
public:
	MemoryRegion();
	void setup(void* emptySpacesMemory, int32_t emptySpacesMemorySize, uint32_t regionBegin, uint32_t regionEnd,
	           CacheManager* cacheManager);
	void* alloc(uint32_t requiredSize, bool makeStealable, void* thingNotToStealFrom);
	size_t nallocx(size_t size) { return padSize(size); }
	uint32_t shortenRight(void* address, uint32_t newSize);
	uint32_t shortenLeft(void* address, uint32_t amountToShorten, uint32_t numBytesToMoveRightIfSuccessful = 0);
	void extend(void* address, uint32_t minAmountToExtend, uint32_t idealAmountToExtend,
	            uint32_t* getAmountExtendedLeft, uint32_t* getAmountExtendedRight, void* thingNotToStealFrom);
	uint32_t extendRightAsMuchAsEasilyPossible(void* spaceAddress);
	void dealloc(void* address);
	void verifyMemoryNotFree(void* address, uint32_t spaceSize);

	uint32_t start;
	uint32_t end;

	CacheManager& cache_manager() {
		if (cache_manager_) {
			return *cache_manager_;
		}
		throw deluge::exception::NO_CACHE_FOR_REGION;
	}

#if ALPHA_OR_BETA_VERSION
	char const* name; // For debugging messages only.
#endif
	OrderedResizeableArrayWithMultiWordKey emptySpaces;

private:
	friend class CacheManager;
	friend class GeneralMemoryAllocator;
	// manages "stealables" for a memory region, only used in external stealable region
	CacheManager* cache_manager_;
	uint32_t numAllocations_{0};
	uint32_t pivot_{pivot_big}; // items smaller than pivot allocate to left, larger to right
	size_t maxAlign_ = max_align_big;
	// not size_t, it needs to  be  compared to results of pointer subtractions that can be negative
	ptrdiff_t minAlign_ = min_align_big;
	void markSpaceAsEmpty(uint32_t spaceStart, uint32_t spaceSize, bool mayLookLeft = true, bool mayLookRight = true);
	NeighbouringMemoryGrabAttemptResult
	attemptToGrabNeighbouringMemory(void* originalSpaceAddress, int32_t originalSpaceSize, int32_t minAmountToExtend,
	                                int32_t idealAmountToExtend, void* thingNotToStealFrom,
	                                uint32_t markWithTraversalNo = 0, bool originalSpaceNeedsStealing = false);

	void writeTempHeadersBeforeASteal(uint32_t newStartAddress, uint32_t newSize);
	void sanityCheck();
	uint32_t padSize(uint32_t requiredSize);
};
