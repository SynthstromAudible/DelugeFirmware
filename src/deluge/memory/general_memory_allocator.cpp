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

#include "memory/general_memory_allocator.h"
#include "libdeluge/memory.h"

#include "definitions_cxx.hpp"
#include "io/debug/log.h"
#include "memory/stealable.h"
#include "processing/engines/audio_engine.h"
#include <cstring>

namespace {
// Under DELUGE_DETERMINISTIC_ALLOC (the off-target sim / golden builds only) every block is zeroed before it's
// handed out, so any read-before-write resolves to a defined 0 instead of recycled-block content. This makes the
// offline golden render independent of the GMA's allocation *layout*: a refactor that shifts what lands where can
// no longer flip a fixture through a latent uninitialised read. Firmware builds leave this off — the no-op inlines
// away, preserving their exact behaviour and allocation-time performance.
[[gnu::always_inline]] inline void* finalizeAlloc([[maybe_unused]] void* address,
                                                  [[maybe_unused]] uint32_t requiredSize) {
#ifdef DELUGE_DETERMINISTIC_ALLOC
	if (address != nullptr) {
		memset(address, 0, requiredSize);
	}
#endif
	return address;
}
} // namespace

// these are never used directly, they're just reserving raw memory for use in the allocator  and clang tidy is unhappy
// NOLINTBEGIN
// TODO: Check if these have the right size
PLACE_INTERNAL_FRUNK char emptySpacesMemory[sizeof(EmptySpaceRecord) * 512];
PLACE_INTERNAL_FRUNK char emptySpacesMemoryInternal[sizeof(EmptySpaceRecord) * 1024];
PLACE_INTERNAL_FRUNK char emptySpacesMemoryInternalSmall[sizeof(EmptySpaceRecord) * 256];
PLACE_INTERNAL_FRUNK char emptySpacesMemoryGeneral[sizeof(EmptySpaceRecord) * 256];
PLACE_INTERNAL_FRUNK char emptySpacesMemoryGeneralSmall[sizeof(EmptySpaceRecord) * 256];
extern uint32_t __frunk_bss_end;
extern uint32_t __frunk_slack_end;
extern uint32_t __sdram_bss_start;
extern uint32_t __sdram_bss_end;
extern uint32_t __heap_end;
extern uint32_t program_stack_start;
extern uint32_t program_stack_end;
// NOLINTEND
GeneralMemoryAllocator::GeneralMemoryAllocator() : lock(false) {
	uint32_t external_small_end = deluge_memory_external_end();
	uint32_t external_small_start = external_small_end - RESERVED_EXTERNAL_SMALL_ALLOCATOR;
	uint32_t external_end = external_small_start;
	uint32_t external_start = external_small_start - RESERVED_EXTERNAL_ALLOCATOR;
	uint32_t stealable_end = external_start;
	// NOLINTBEGIN
	// clang tidy hates both reinterpret and c style casts but linker output is only meaningful when taking address
	auto stealable_start = (uint32_t)&__sdram_bss_end;

	auto internal_small_start = (uint32_t)&__frunk_bss_end;
	auto internal_small_end = (uint32_t)&__frunk_slack_end;
	// NOLINTEND

	// Internal (fast SRAM) heap bounds come from the BSP's region descriptor
	// (libdeluge/memory.h), not raw linker symbols: the BSP knows its own layout
	// (e.g. the Rust BSP places per-mode exception stacks between the heap and the
	// program stack, so &program_stack_start is NOT the heap top there). The
	// program_stack_start symbol stays reserved for checkStack()'s stack guard.
	DelugeMemoryRegion internalRegion = {nullptr, 0, DELUGE_MEM_FAST_INTERNAL};
	for (uint8_t i = 0, n = deluge_memory_region_count(); i < n; ++i) {
		DelugeMemoryRegion r;
		if (deluge_memory_region(i, &r) == DELUGE_OK && r.kind == DELUGE_MEM_FAST_INTERNAL) {
			internalRegion = r;
			break;
		}
	}
	auto internal_start = (uint32_t)internalRegion.base;
	auto internal_end = internal_start + internalRegion.size;
	regions[MEMORY_REGION_STEALABLE].name = "stealable";
	regions[MEMORY_REGION_INTERNAL].name = "internal";
	regions[MEMORY_REGION_EXTERNAL].name = "external";
	regions[MEMORY_REGION_EXTERNAL_SMALL].name = "small external";
	regions[MEMORY_REGION_INTERNAL_SMALL].name = "small internal";

	regions[MEMORY_REGION_STEALABLE].setup(emptySpacesMemory, sizeof(emptySpacesMemory), stealable_start, stealable_end,
	                                       &cacheManager);
	regions[MEMORY_REGION_EXTERNAL].setup(emptySpacesMemoryGeneral, sizeof(emptySpacesMemoryGeneral), external_start,
	                                      external_end, nullptr);
	regions[MEMORY_REGION_EXTERNAL_SMALL].setup(emptySpacesMemoryGeneralSmall, sizeof(emptySpacesMemoryGeneralSmall),
	                                            external_small_start, external_small_end, nullptr);
	regions[MEMORY_REGION_EXTERNAL_SMALL].minAlign_ = 16;
	regions[MEMORY_REGION_EXTERNAL_SMALL].pivot_ = 64;
	regions[MEMORY_REGION_INTERNAL].setup(emptySpacesMemoryInternal, sizeof(emptySpacesMemoryInternal), internal_start,
	                                      internal_end, nullptr);

	regions[MEMORY_REGION_INTERNAL_SMALL].setup(emptySpacesMemoryInternalSmall, sizeof(emptySpacesMemoryInternalSmall),
	                                            internal_small_start, internal_small_end, nullptr);
	regions[MEMORY_REGION_INTERNAL_SMALL].minAlign_ = 16;
	regions[MEMORY_REGION_INTERNAL_SMALL].pivot_ = 64;
}
constexpr size_t kInternalSwitchSize = 128;
constexpr size_t kExternalSwitchSize = 128;
int32_t closestDistance = 2147483647;

void GeneralMemoryAllocator::checkStack(char const* caller) {
#if ALPHA_OR_BETA_VERSION

	// The stack-collision guard measures headroom between the live stack pointer and
	// `program_stack_start` — valid only where the stack sits in a known region just past
	// the heap (the SoC's SRAM layout). A build whose BSP can't describe such a region
	// leaves `program_stack_end == program_stack_start` (e.g. the host sim, which runs on
	// the OS-managed native stack — an unrelated, far-away address). There the subtraction
	// underflows to a huge bogus "distance" and would false-trigger E338; the OS guard page
	// already protects that build, so skip.
	if (&program_stack_start == &program_stack_end) {
		return;
	}

	char a;

	// The collision guard measures headroom against `program_stack_start`, which is
	// valid ONLY while running on the main program stack. On the Rust/Embassy BSP the
	// audio render also runs from the audio interrupt-executor, which executes in SYS
	// mode on the *preempted* context's stack — e.g. the worker fiber's SDRAM stack
	// during a song load. There `&a` lies far outside [program_stack_start,
	// program_stack_end], so the distance is meaningless and would false-trigger E338
	// every render (logged as a huge/negative "bytes in stack" + COLLISION, then a
	// freezeWithError blit from interrupt context that corrupts the display). Only run
	// the guard when the live SP is actually on the main stack.
	uintptr_t sp = (uintptr_t)&a;
	if (sp < (uintptr_t)&program_stack_start || sp > (uintptr_t)&program_stack_end) {
		return;
	}

	int32_t distance = (int32_t)&a - (uint32_t)&program_stack_start;
	if (distance < closestDistance) {
		closestDistance = distance;

		D_PRINTLN("%d bytes in stack %d free bytes in stack at %s", (uint32_t)&program_stack_end - (int32_t)&a,
		          distance, caller);
		if (distance < 200) {
			FREEZE_WITH_ERROR("E338");
			D_PRINTLN("COLLISION");
		}
	}
#endif
}

#if TEST_GENERAL_MEMORY_ALLOCATION
uint32_t totalMallocTime = 0;
int32_t numMallocTimes = 0;
#endif
extern "C" void* delugeAlloc(unsigned int requiredSize, bool mayUseOnChipRam) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, mayUseOnChipRam, false, nullptr);
}
extern "C" void delugeDealloc(void* address) {
#ifdef IN_UNIT_TESTS
	free(address);
#else
	GeneralMemoryAllocator::get().dealloc(address);
#endif
}
void* GeneralMemoryAllocator::allocExternal(uint32_t requiredSize) {

	if (lock) {
		return nullptr; // Prevent any weird loops in freeSomeStealableMemory(), which mostly would only be bad cos they
		                // could extend the stack an unspecified amount
	}

	lock = true;
	void* address = nullptr;
	if (requiredSize < kExternalSwitchSize) {
		address = regions[MEMORY_REGION_EXTERNAL_SMALL].alloc(requiredSize, false, NULL);
	}
	// if it's a large object or the small object allocator was full stick it in the big one
	if (address == nullptr) {
		address = regions[MEMORY_REGION_EXTERNAL].alloc(requiredSize, false, NULL);
	}
	lock = false;
	if (!address) {
		// FREEZE_WITH_ERROR("M998");
		return nullptr;
	}
	return address;
}

void* GeneralMemoryAllocator::allocInternal(uint32_t requiredSize) {

	if (lock) {
		return nullptr; // Prevent any weird loops in freeSomeStealableMemory(), which mostly would only be bad cos they
		                // could extend the stack an unspecified amount
	}

	lock = true;
	void* address = nullptr;
	if (requiredSize < kInternalSwitchSize) {
		address = regions[MEMORY_REGION_INTERNAL_SMALL].alloc(requiredSize, false, NULL);
	}
	// if it's a large object or the small object allocator was full stick it in the big one
	if (address == nullptr) {
		address = regions[MEMORY_REGION_INTERNAL].alloc(requiredSize, false, NULL);
	}
	lock = false;
	if (address == nullptr) {
		// FREEZE_WITH_ERROR("M998");
	}
	return address;
}
void GeneralMemoryAllocator::deallocExternal(void* address) {
	regions[getRegion(address)].dealloc(address);
}

// Watch the heck out - in the older V3.1 branch, this had one less argument - makeStealable was missing - so in code
// from there, thingNotToStealFrom could be interpreted as makeStealable! requiredSize 0 means get biggest allocation
// available.
void* GeneralMemoryAllocator::alloc(uint32_t requiredSize, bool mayUseOnChipRam, bool makeStealable,
                                    void* thingNotToStealFrom) {

	if (lock) {
		return nullptr; // Prevent any weird loops in freeSomeStealableMemory(), which mostly would only be bad cos they
		                // could extend the stack an unspecified amount
	}

	void* address = nullptr;

	// Only allow allocating stealables in stelable region
	if (!makeStealable) {
		// If internal is allowed, try that first
		if (mayUseOnChipRam) {
			address = allocInternal(requiredSize);

			if (address != nullptr) {
				return finalizeAlloc(address, requiredSize);
			}

			AudioEngine::logAction("internal allocation failed");
		}

		// Second try external region
		address = allocExternal(requiredSize);

		if (address) {
			return finalizeAlloc(address, requiredSize);
		}

		AudioEngine::logAction("external allocation failed");

		D_PRINTLN("Dire memory, resorting to stealable area");
	}

#if TEST_GENERAL_MEMORY_ALLOCATION
	if (requiredSize < 1) {
		D_PRINTLN("alloc too little a bit");
		while (1) {}
	}
#endif

	lock = true;
	address = regions[MEMORY_REGION_STEALABLE].alloc(requiredSize, makeStealable, thingNotToStealFrom);
	lock = false;
	return finalizeAlloc(address, requiredSize);
}

uint32_t GeneralMemoryAllocator::getAllocatedSize(void* address) {
	uint32_t* header = (uint32_t*)((uint32_t)address - 4);
	return (*header & SPACE_SIZE_MASK);
}

int32_t GeneralMemoryAllocator::getRegion(void* address) {
	uint32_t value = (uint32_t)address;
	if (value >= regions[MEMORY_REGION_INTERNAL].start && value < regions[MEMORY_REGION_INTERNAL].end) {
		return MEMORY_REGION_INTERNAL;
	}
	else if (value >= regions[MEMORY_REGION_STEALABLE].start && value < regions[MEMORY_REGION_STEALABLE].end) {
		return MEMORY_REGION_STEALABLE;
	}
	else if (value >= regions[MEMORY_REGION_EXTERNAL].start && value < regions[MEMORY_REGION_EXTERNAL].end) {
		return MEMORY_REGION_EXTERNAL;
	}
	else if (value >= regions[MEMORY_REGION_EXTERNAL_SMALL].start
	         && value < regions[MEMORY_REGION_EXTERNAL_SMALL].end) {
		return MEMORY_REGION_EXTERNAL_SMALL;
	}
	else if (value >= regions[MEMORY_REGION_INTERNAL_SMALL].start
	         && value < regions[MEMORY_REGION_INTERNAL_SMALL].end) {
		return MEMORY_REGION_INTERNAL_SMALL;
	}

	FREEZE_WITH_ERROR("E339");
	return 0;
}

// Returns new size
uint32_t GeneralMemoryAllocator::shortenRight(void* address, uint32_t newSize) {
	return regions[getRegion(address)].shortenRight(address, newSize);
}

// Returns how much it was shortened by
uint32_t GeneralMemoryAllocator::shortenLeft(void* address, uint32_t amountToShorten,
                                             uint32_t numBytesToMoveRightIfSuccessful) {
	return regions[getRegion(address)].shortenLeft(address, amountToShorten, numBytesToMoveRightIfSuccessful);
}

void GeneralMemoryAllocator::extend(void* address, uint32_t minAmountToExtend, uint32_t idealAmountToExtend,
                                    uint32_t* __restrict__ getAmountExtendedLeft,
                                    uint32_t* __restrict__ getAmountExtendedRight, void* thingNotToStealFrom) {

	*getAmountExtendedLeft = 0;
	*getAmountExtendedRight = 0;

	if (lock) {
		return;
	}

	lock = true;
	regions[getRegion(address)].extend(address, minAmountToExtend, idealAmountToExtend, getAmountExtendedLeft,
	                                   getAmountExtendedRight, thingNotToStealFrom);
	lock = false;
}

uint32_t GeneralMemoryAllocator::extendRightAsMuchAsEasilyPossible(void* address) {
	return regions[getRegion(address)].extendRightAsMuchAsEasilyPossible(address);
}

void GeneralMemoryAllocator::dealloc(void* address) {
	if (address == nullptr) [[unlikely]] {
		return;
	}
	regions[getRegion(address)].dealloc(address);
}

void GeneralMemoryAllocator::putStealableInQueue(Stealable* stealable, StealableQueue q) {
	MemoryRegion& region = regions[getRegion(stealable)];
	region.cache_manager().QueueForReclamation(q, stealable);
}

void GeneralMemoryAllocator::putStealableInAppropriateQueue(Stealable* stealable) {
	StealableQueue q = stealable->getAppropriateQueue();
	putStealableInQueue(stealable, q);
}
