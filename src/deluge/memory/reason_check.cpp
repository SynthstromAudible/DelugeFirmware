/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

#include "memory/reason_check.h"

#if REASON_CHECK

#include "definitions_cxx.hpp"
#include "io/debug/log.h"

#if defined(__SANITIZE_ADDRESS__)
#include <dlfcn.h> // host-sim: turn a PIE-relative return address into a file offset addr2line can resolve
#endif

namespace reason_check {
namespace {

// Power-of-two open-addressing table keyed by cluster address. 4096 slots * 16 bytes = 64 KiB in SDRAM. Clusters are
// >=32 KiB, so on a 64 MiB board the live population can't exceed ~2k - this stays well under a 0.75 load factor. Zero-
// initialised by the SDRAM clear at startup, so every bucket starts empty.
constexpr uint32_t kCapacity = 1u << 12;
constexpr uint32_t kMask = kCapacity - 1;
constexpr uint32_t kEmpty = 0u;     // address sentinel: free bucket
constexpr uint32_t kTombstone = 1u; // address sentinel: deleted bucket

struct Slot {
	uint32_t address;
	uint32_t callsite; // caller that took the cluster's first (currently-outstanding) reason
	int32_t reasons;   // outstanding reasons we've seen for this cluster
	uint32_t epoch;    // reason epoch when the first reason was taken; lets snapshot() tell new from old
};

PLACE_SDRAM_BSS Slot slots[kCapacity];
uint32_t liveCount = 0;   // clusters with >=1 outstanding reason
uint32_t reasonEpoch = 0; // bumped each time a cluster goes from 0 -> 1 reasons
bool warnedFull = false;

inline uint32_t hashAddress(uint32_t address) {
	// Clusters are >=16-byte aligned; drop the low bits and mix (Knuth).
	uint32_t h = (address >> 4) * 2654435761u;
	return (h >> 13) & kMask;
}

// Find the live slot for `key`, or nullptr.
Slot* findSlot(uint32_t key) {
	if (key == kEmpty || key == kTombstone) {
		return nullptr;
	}
	uint32_t b = hashAddress(key);
	for (uint32_t probes = 0; probes < kCapacity; probes++) {
		uint32_t slotAddr = slots[b].address;
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

// Find the slot for `key`, inserting a fresh (zeroed) one if absent. Returns nullptr only if the table is full.
Slot* findOrInsert(uint32_t key) {
	uint32_t b = hashAddress(key);
	int32_t firstTomb = -1;
	for (uint32_t probes = 0; probes < kCapacity; probes++) {
		uint32_t slotAddr = slots[b].address;
		if (slotAddr == key) {
			return &slots[b];
		}
		if (slotAddr == kEmpty) {
			uint32_t dest = (firstTomb >= 0) ? (uint32_t)firstTomb : b;
			slots[dest].address = key;
			slots[dest].callsite = 0;
			slots[dest].reasons = 0;
			slots[dest].epoch = 0;
			return &slots[dest];
		}
		if (slotAddr == kTombstone && firstTomb < 0) {
			firstTomb = (int32_t)b;
		}
		b = (b + 1) & kMask;
	}
	if (firstTomb >= 0) {
		slots[firstTomb].address = key;
		slots[firstTomb].callsite = 0;
		slots[firstTomb].reasons = 0;
		slots[firstTomb].epoch = 0;
		return &slots[firstTomb];
	}
	return nullptr;
}

void removeSlot(Slot* s) {
	s->address = kTombstone;
}

} // namespace

void onAdd(void* cluster, void* callsite) {
	uint32_t key = (uint32_t)cluster;
	if (key == kEmpty || key == kTombstone) {
		return;
	}
	Slot* s = findOrInsert(key);
	if (s == nullptr) {
		if (!warnedFull) {
			warnedFull = true;
			D_PRINTLN("REASON_CHECK table full (%d clusters) - tracking degraded", liveCount);
		}
		return;
	}
	if (s->reasons == 0) {
		// First reason on this cluster: it goes live, attributed to this call-site.
		s->callsite = (uint32_t)callsite;
		s->epoch = ++reasonEpoch;
		liveCount++;
	}
	s->reasons++;
}

void onRemove(void* cluster) {
	Slot* s = findSlot((uint32_t)cluster);
	if (s == nullptr) {
		return; // untracked (e.g. reason taken before tracking began) - leave it alone
	}
	s->reasons--;
	if (s->reasons <= 0) {
		liveCount--;
		removeSlot(s);
	}
}

void onSteal(void* cluster, int32_t numReasons) {
	if (numReasons != 0) {
		Slot* s = findSlot((uint32_t)cluster);
		uint32_t callsite = (s != nullptr) ? s->callsite : 0;
		D_PRINTLN("REASON_CHECK use-after-steal: cluster %x stolen with %d reason(s) still held; first reason taken at "
		          "call-site %x",
		          (uint32_t)cluster, numReasons, callsite);
		FREEZE_WITH_ERROR("RC01");
	}
	forget(cluster);
}

void forget(void* cluster) {
	Slot* s = findSlot((uint32_t)cluster);
	if (s != nullptr) {
		if (s->reasons > 0) {
			liveCount--;
		}
		removeSlot(s);
	}
}

Snapshot snapshot() {
	return reasonEpoch;
}

uint32_t liveClusters() {
	return liveCount;
}

void reportOutstanding(Snapshot since) {
	uint32_t clusters = 0;
	int32_t reasons = 0;
	for (uint32_t i = 0; i < kCapacity; i++) {
		uint32_t addr = slots[i].address;
		if (addr == kEmpty || addr == kTombstone) {
			continue;
		}
		if (slots[i].reasons <= 0 || slots[i].epoch <= since) {
			continue;
		}
		clusters++;
		reasons += slots[i].reasons;
		uint32_t callsite = slots[i].callsite;
#if defined(__SANITIZE_ADDRESS__)
		// The host-sim is a PIE, so the stored return address has a random load base. Rebase it to a file offset that
		// `addr2line -e deluge_host <offset>` resolves directly.
		Dl_info info;
		if (dladdr((void*)callsite, &info) && info.dli_fbase) {
			callsite -= (uint32_t)(uintptr_t)info.dli_fbase;
		}
#endif
		D_PRINTLN("  stealable %x : %d reason(s) outstanding, first taken at call-site %x (resolve with addr2line)", addr,
		          slots[i].reasons, callsite);
	}
	if (clusters == 0) {
		D_PRINTLN("REASON_CHECK: no reasons outstanding since snapshot %d", since);
	}
	else {
		D_PRINTLN("REASON_CHECK: %d stealable(s) (Clusters/AudioFiles) holding %d reason(s) outstanding since snapshot "
		          "%d (likely pinned/leaked)",
		          clusters, reasons, since);
	}
}

} // namespace reason_check

#endif // REASON_CHECK
