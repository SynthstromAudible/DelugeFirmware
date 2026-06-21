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

#pragma once
#include <cstdint>

class MemoryRegion;

/// Abstract reclamation hook the allocator calls when it is out of space.
///
/// This inverts the old dependency: previously MemoryRegion reached up into the
/// concrete CacheManager (and through it into Cluster/Sample) to evict cache
/// memory. Now the allocator knows only this interface — "ask someone to free
/// some bytes" — and a resource layer (CacheManager) implements it and
/// registers itself. The arrows point down: allocator → Reclaimer ← cache pool.
///
/// Phase 1 of docs/dev/allocator_implementation_plan.md. The signature mirrors
/// the old CacheManager::ReclaimMemory contract exactly so the inversion is
/// behavior-preserving; the slab redesign (Phase 3) replaces it with a simpler
/// bool reclaim(bytes, dontReclaimFrom) once uniform slots remove the
/// neighbour-grab machinery.
class Reclaimer {
public:
	virtual ~Reclaimer() = default;

	/// Try to reclaim at least `totalSizeNeeded` bytes within `region`, never
	/// evicting `thingNotToStealFrom`. Returns the address of a reclaimed block
	/// (0 on failure); the block's size is returned via `foundSpaceSize`. A
	/// `totalSizeNeeded` of 0 means "any reclaimable memory will do".
	virtual uint32_t reclaim(MemoryRegion& region, int32_t totalSizeNeeded, void* thingNotToStealFrom,
	                         int32_t* __restrict__ foundSpaceSize) = 0;
};
