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

#include "definitions.h"
#include <cstdint>

#if MEM_GUARD

// Side table mapping a live allocation's user-address to the call-site that allocated it and the originally-requested
// size. It deliberately uses its own statically-reserved storage and never touches the general allocator, so it is safe
// to call from inside alloc()/dealloc() (which run under GeneralMemoryAllocator::lock). This is what turns a detected
// corruption ("heap is broken at address X") into an attributed one ("the block at X was allocated from <call-site>").
namespace mem_guard {

struct AllocInfo {
	uintptr_t callsite;     // __builtin_return_address of the allocating caller (resolve offline with addr2line)
	uint32_t requestedSize; // original, un-padded size passed to alloc
};

// Opaque marker for "the set of allocations live at a moment in time". Internally it is the value of the monotonic
// allocation epoch counter; an allocation belongs to a snapshot's "after" set iff its epoch is strictly greater.
using Snapshot = uint32_t;

// Record (or overwrite) the metadata for a live allocation at `address`.
void recordAlloc(void* address, uint32_t requestedSize, void* callsite);

// Look up metadata for `address`. Returns true and fills *out (if non-null) when found.
bool lookupAlloc(void* address, AllocInfo* out);

// Forget the allocation at `address`. Returns true if an entry existed (false => wild/double free of an untracked ptr).
bool removeAlloc(void* address);

// Re-key after an in-place resize/move (shortenLeft / extend can move the user-address; shortenRight changes the size).
// No-op if `oldAddress` was not tracked.
void updateAllocKey(void* oldAddress, void* newAddress, uint32_t newRequestedSize);

// ---- Leak hunting --------------------------------------------------------------------------------------------------
//
// The table above already knows every live allocation. These turn that into a leak report. The intended workflow is a
// round-trip diff: reach a known-idle UI state, take a snapshot, perform the suspect operation, return to the SAME idle
// state, then report. Anything still live that was allocated after the snapshot is a leak, attributed to its call-site.
//
//     mem_guard::Snapshot before = mem_guard::snapshot();
//     // ... load a song / open a clip / whatever you suspect ...
//     // ... tear it back down to the original state ...
//     mem_guard::reportOutstanding(before);   // prints only what leaked across the round trip

// Capture the current allocation epoch. Allocations made after this point compare strictly greater.
Snapshot snapshot();

// Number of allocations currently tracked as live.
uint32_t liveAllocations();

// Walk the table and log every live allocation whose epoch is > `since`, aggregated by call-site and sorted by total
// bytes descending. Pass 0 (the default) to report ALL live allocations. Call-sites are raw return addresses - resolve
// them offline with `arm-none-eabi-addr2line -f -e <deluge.elf> <callsite>`. Must be called from a safe point (not mid
// alloc/dealloc); it does not allocate.
void reportOutstanding(Snapshot since = 0);

} // namespace mem_guard

#endif
