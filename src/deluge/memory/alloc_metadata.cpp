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

#include "memory/alloc_metadata.h"

#if MEM_GUARD

#include "io/debug/log.h"

namespace mem_guard {
namespace {

// Power-of-two open-addressing table. 32768 slots * 12 bytes = 384 KiB, parked in SDRAM (well clear of the allocator's
// own regions). Sized to comfortably hold the Deluge's live small-object population at a <0.75 load factor. Zero-
// initialised by the SDRAM clear at startup, so every bucket starts empty without a constructor.
constexpr uint32_t kCapacity = 1u << 15;
constexpr uint32_t kMask = kCapacity - 1;
constexpr uint32_t kEmpty = 0u;     // address sentinel: free bucket (0 is never a valid heap address)
constexpr uint32_t kTombstone = 1u; // address sentinel: deleted bucket (1 is never a valid heap address)

struct Slot {
	uint32_t address;
	uint32_t callsite;
	uint32_t requestedSize;
};

PLACE_SDRAM_BSS Slot slots[kCapacity];
uint32_t liveCount = 0;
bool warnedFull = false;

inline uint32_t hashAddress(uint32_t address) {
	// Allocations are >=16-byte aligned, so the low 4 bits carry no information - drop them, then mix (Knuth).
	uint32_t h = (address >> 4) * 2654435761u;
	return (h >> 13) & kMask;
}

} // namespace

void recordAlloc(void* address, uint32_t requestedSize, void* callsite) {
	uint32_t key = (uint32_t)address;
	if (key == kEmpty || key == kTombstone) {
		return;
	}
	uint32_t b = hashAddress(key);
	int32_t firstTomb = -1;
	for (uint32_t probes = 0; probes < kCapacity; probes++) {
		uint32_t slotAddr = slots[b].address;
		if (slotAddr == key) { // already present - overwrite (e.g. memory reused at the same address)
			slots[b].callsite = (uint32_t)callsite;
			slots[b].requestedSize = requestedSize;
			return;
		}
		if (slotAddr == kEmpty) {
			uint32_t dest = (firstTomb >= 0) ? (uint32_t)firstTomb : b;
			slots[dest].address = key;
			slots[dest].callsite = (uint32_t)callsite;
			slots[dest].requestedSize = requestedSize;
			liveCount++;
			return;
		}
		if (slotAddr == kTombstone && firstTomb < 0) {
			firstTomb = (int32_t)b;
		}
		b = (b + 1) & kMask;
	}
	// Probed every slot without finding an empty bucket. Reuse a tombstone if we passed one, otherwise the table is
	// genuinely full: degrade gracefully (this allocation just won't be attributed) rather than freezing.
	if (firstTomb >= 0) {
		slots[firstTomb].address = key;
		slots[firstTomb].callsite = (uint32_t)callsite;
		slots[firstTomb].requestedSize = requestedSize;
		liveCount++;
		return;
	}
	if (!warnedFull) {
		warnedFull = true;
		D_PRINTLN("MEM_GUARD alloc metadata table full (%d live) - attribution degraded", liveCount);
	}
}

bool lookupAlloc(void* address, AllocInfo* out) {
	uint32_t key = (uint32_t)address;
	if (key == kEmpty || key == kTombstone) {
		return false;
	}
	uint32_t b = hashAddress(key);
	for (uint32_t probes = 0; probes < kCapacity; probes++) {
		uint32_t slotAddr = slots[b].address;
		if (slotAddr == kEmpty) {
			return false;
		}
		if (slotAddr == key) {
			if (out) {
				out->callsite = slots[b].callsite;
				out->requestedSize = slots[b].requestedSize;
			}
			return true;
		}
		b = (b + 1) & kMask;
	}
	return false;
}

bool removeAlloc(void* address) {
	uint32_t key = (uint32_t)address;
	if (key == kEmpty || key == kTombstone) {
		return false;
	}
	uint32_t b = hashAddress(key);
	for (uint32_t probes = 0; probes < kCapacity; probes++) {
		uint32_t slotAddr = slots[b].address;
		if (slotAddr == kEmpty) {
			return false;
		}
		if (slotAddr == key) {
			slots[b].address = kTombstone;
			liveCount--;
			return true;
		}
		b = (b + 1) & kMask;
	}
	return false;
}

void updateAllocKey(void* oldAddress, void* newAddress, uint32_t newRequestedSize) {
	AllocInfo info{};
	if (!lookupAlloc(oldAddress, &info)) {
		return; // not a tracked allocation - leave it alone
	}
	if (oldAddress != newAddress) {
		removeAlloc(oldAddress);
	}
	recordAlloc(newAddress, newRequestedSize, (void*)info.callsite);
}

} // namespace mem_guard

#endif
