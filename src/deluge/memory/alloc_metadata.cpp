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

// Power-of-two open-addressing table parked in SDRAM (well clear of the allocator's own regions). Sized to comfortably
// hold the Deluge's live small-object population at a <0.75 load factor. Zero-initialised by the SDRAM clear at
// startup, so every bucket starts empty without a constructor. (address/callsite are pointer-width: 16 B/slot on the
// 32-bit firmware, 24 B/slot on a 64-bit host.)
constexpr uint32_t kCapacity = 1u << 15;
constexpr uint32_t kMask = kCapacity - 1;
constexpr uintptr_t kEmpty = 0u;     // address sentinel: free bucket (0 is never a valid heap address)
constexpr uintptr_t kTombstone = 1u; // address sentinel: deleted bucket (1 is never a valid heap address)

struct Slot {
	uintptr_t address;
	uintptr_t callsite;
	uint32_t requestedSize;
	uint32_t epoch; // monotonic allocation sequence number; lets a snapshot tell "allocated before" from "after"
};

PLACE_SDRAM_BSS Slot slots[kCapacity];
uint32_t liveCount = 0;
uint32_t allocEpoch = 0; // bumped on every genuinely-new allocation; never wraps in practice (1 per alloc)
bool warnedFull = false;

inline uint32_t hashAddress(uintptr_t address) {
	// Allocations are >=16-byte aligned, so the low 4 bits carry no information - drop them, then mix (Knuth).
	uint32_t lo = (uint32_t)(address >> 4);
	if constexpr (sizeof(uintptr_t) > sizeof(uint32_t)) {
		lo ^= (uint32_t)(address >> 32); // fold the high half on 64-bit; not instantiated (no UB) on 32-bit
	}
	uint32_t h = lo * 2654435761u;
	return (h >> 13) & kMask;
}

// Shared insert/overwrite core. `epoch` is supplied by the caller so a genuine allocation gets a fresh epoch while an
// in-place re-key (updateAllocKey) can carry the original epoch forward - a resized block is not a new allocation.
void recordAllocWithEpoch(uintptr_t key, uint32_t requestedSize, uintptr_t callsite, uint32_t epoch) {
	if (key == kEmpty || key == kTombstone) {
		return;
	}
	uint32_t b = hashAddress(key);
	int32_t firstTomb = -1;
	for (uint32_t probes = 0; probes < kCapacity; probes++) {
		uintptr_t slotAddr = slots[b].address;
		if (slotAddr == key) { // already present - overwrite (e.g. memory reused at the same address)
			slots[b].callsite = callsite;
			slots[b].requestedSize = requestedSize;
			slots[b].epoch = epoch;
			return;
		}
		if (slotAddr == kEmpty) {
			uint32_t dest = (firstTomb >= 0) ? (uint32_t)firstTomb : b;
			slots[dest].address = key;
			slots[dest].callsite = callsite;
			slots[dest].requestedSize = requestedSize;
			slots[dest].epoch = epoch;
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
		slots[firstTomb].callsite = callsite;
		slots[firstTomb].requestedSize = requestedSize;
		slots[firstTomb].epoch = epoch;
		liveCount++;
		return;
	}
	if (!warnedFull) {
		warnedFull = true;
		D_PRINTLN("MEM_GUARD alloc metadata table full (%d live) - attribution degraded", liveCount);
	}
}

// Return the live slot for `key`, or nullptr if not tracked.
Slot* findSlot(uintptr_t key) {
	if (key == kEmpty || key == kTombstone) {
		return nullptr;
	}
	uint32_t b = hashAddress(key);
	for (uint32_t probes = 0; probes < kCapacity; probes++) {
		uintptr_t slotAddr = slots[b].address;
		if (slotAddr == kEmpty) {
			return nullptr;
		}
		if (slotAddr == key) {
			return &slots[b];
		}
		b = (b + 1) & kMask;
	}
	return nullptr;
}

} // namespace

void recordAlloc(void* address, uint32_t requestedSize, void* callsite) {
	recordAllocWithEpoch((uintptr_t)address, requestedSize, (uintptr_t)callsite, ++allocEpoch);
}

bool lookupAlloc(void* address, AllocInfo* out) {
	uintptr_t key = (uintptr_t)address;
	if (key == kEmpty || key == kTombstone) {
		return false;
	}
	uint32_t b = hashAddress(key);
	for (uint32_t probes = 0; probes < kCapacity; probes++) {
		uintptr_t slotAddr = slots[b].address;
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
	uintptr_t key = (uintptr_t)address;
	if (key == kEmpty || key == kTombstone) {
		return false;
	}
	uint32_t b = hashAddress(key);
	for (uint32_t probes = 0; probes < kCapacity; probes++) {
		uintptr_t slotAddr = slots[b].address;
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
	Slot* old = findSlot((uintptr_t)oldAddress);
	if (old == nullptr) {
		return; // not a tracked allocation - leave it alone
	}
	// A resize/move is the SAME allocation: carry its call-site and epoch across so a snapshot diff doesn't mistake a
	// grown-after-snapshot block for a fresh leak.
	uintptr_t callsite = old->callsite;
	uint32_t epoch = old->epoch;
	if (oldAddress != newAddress) {
		removeAlloc(oldAddress);
	}
	recordAllocWithEpoch((uintptr_t)newAddress, newRequestedSize, callsite, epoch);
}

Snapshot snapshot() {
	return allocEpoch;
}

uint32_t liveAllocations() {
	return liveCount;
}

void reportOutstanding(Snapshot since) {
	// Aggregate leaked blocks by call-site into a fixed, statically-reserved bucket table (no allocation - we may be
	// called from inside the allocator's world). Distinct call-sites in a leak set are few; if we somehow overflow, we
	// fold the rest into a catch-all rather than miss them.
	constexpr uint32_t kMaxBuckets = 256;
	struct Bucket {
		uintptr_t callsite;
		uint32_t count;
		uint32_t bytes;
	};
	PLACE_SDRAM_BSS static Bucket buckets[kMaxBuckets];
	uint32_t numBuckets = 0;
	uint32_t overflowCount = 0;
	uint32_t overflowBytes = 0;
	uint32_t totalCount = 0;
	uint32_t totalBytes = 0;

	for (uint32_t i = 0; i < kCapacity; i++) {
		uintptr_t addr = slots[i].address;
		if (addr == kEmpty || addr == kTombstone) {
			continue;
		}
		if (slots[i].epoch <= since) {
			continue; // allocated at or before the snapshot - not part of this diff
		}
		uintptr_t cs = slots[i].callsite;
		uint32_t sz = slots[i].requestedSize;
		totalCount++;
		totalBytes += sz;

		uint32_t j = 0;
		for (; j < numBuckets; j++) {
			if (buckets[j].callsite == cs) {
				buckets[j].count++;
				buckets[j].bytes += sz;
				break;
			}
		}
		if (j == numBuckets) {
			if (numBuckets < kMaxBuckets) {
				buckets[numBuckets].callsite = cs;
				buckets[numBuckets].count = 1;
				buckets[numBuckets].bytes = sz;
				numBuckets++;
			}
			else {
				overflowCount++;
				overflowBytes += sz;
			}
		}
	}

	if (totalCount == 0) {
		D_PRINTLN("MEM_GUARD leak report: nothing outstanding since snapshot %d", since);
		return;
	}

	D_PRINTLN("MEM_GUARD leak report: %d blocks, %d bytes outstanding since snapshot %d (by call-site, resolve with "
	          "addr2line):",
	          totalCount, totalBytes, since);

	// Selection-sort the buckets by bytes descending so the worst offenders print first. numBuckets is small.
	for (uint32_t a = 0; a < numBuckets; a++) {
		uint32_t best = a;
		for (uint32_t b = a + 1; b < numBuckets; b++) {
			if (buckets[b].bytes > buckets[best].bytes) {
				best = b;
			}
		}
		if (best != a) {
			Bucket tmp = buckets[a];
			buckets[a] = buckets[best];
			buckets[best] = tmp;
		}
		D_PRINTLN("  callsite %zx : %d blocks, %d bytes", (size_t)buckets[a].callsite, buckets[a].count,
		          buckets[a].bytes);
	}
	if (overflowCount) {
		D_PRINTLN("  (+%d more blocks, %d bytes across additional call-sites)", overflowCount, overflowBytes);
	}
}

} // namespace mem_guard

#endif
