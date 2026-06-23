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
	uint32_t callsite;      // __builtin_return_address of the allocating caller (resolve offline with addr2line)
	uint32_t requestedSize; // original, un-padded size passed to alloc
};

// Record (or overwrite) the metadata for a live allocation at `address`.
void recordAlloc(void* address, uint32_t requestedSize, void* callsite);

// Look up metadata for `address`. Returns true and fills *out (if non-null) when found.
bool lookupAlloc(void* address, AllocInfo* out);

// Forget the allocation at `address`. Returns true if an entry existed (false => wild/double free of an untracked ptr).
bool removeAlloc(void* address);

// Re-key after an in-place resize/move (shortenLeft / extend can move the user-address; shortenRight changes the size).
// No-op if `oldAddress` was not tracked.
void updateAllocKey(void* oldAddress, void* newAddress, uint32_t newRequestedSize);

} // namespace mem_guard

#endif
