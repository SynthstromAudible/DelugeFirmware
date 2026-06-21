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

#include <cstddef>

// The application's allocator heaps, built over the BSP's memory regions
// (deluge_memory_region) and backed by the Rust deluge_alloc core. These free
// functions are the clean way to reach a heap — the typed C++ allocators
// (fast/sdram/external_allocator) and the GeneralMemoryAllocator forwarders
// allocate from here, rather than each owning/locating heaps via
// GeneralMemoryAllocator::get(). The GMA is the stealable/reclaim coordinator;
// heap ownership lives here. DelugeHeap is opaque here (full C ABI in
// libdeluge/alloc.h) so consumers don't need include/.

struct DelugeHeap;

namespace deluge::memory {

/// Build the heaps from deluge_memory_region() (+ the linker-defined frunk region)
/// if not already built. Idempotent; safe to call from any heap accessor.
void init_heaps();

/// Fast internal SRAM heap (DELUGE_MEM_FAST_INTERNAL region).
DelugeHeap* sram_heap();

/// Large external SDRAM heap (DELUGE_MEM_LARGE_EXTERNAL region).
DelugeHeap* sdram_heap();

/// Small-internal ("frunk") SRAM heap — a reserved slack area defined by linker
/// symbols (not a deluge_memory_region). Backs tiny internal allocations so they
/// can't fragment / starve the main SRAM heap. nullptr if the region is empty.
DelugeHeap* frunk_heap();

/// The heap owning `ptr` (by address range), or nullptr if none — used to route
/// frees / resizes to the right heap.
DelugeHeap* owning_heap(void* ptr);

/// Byte span of the SRAM / SDRAM regions (0 if not built). sdram_size() sizes the
/// cluster slab's table so it's never the limiting factor.
std::size_t sram_size();
std::size_t sdram_size();

// Allocation helpers — the clean surface the typed C++ allocators (fast/sdram/
// external_allocator) call, so they need neither GeneralMemoryAllocator::get()
// nor libdeluge/alloc.h. They allocate from the deluge_alloc heaps above; `align`
// is honored. Return nullptr on OOM.

/// SRAM-preferred with SDRAM fallback (== the old allocMaxSpeed).
void* alloc_fast(std::size_t size, std::size_t align);
/// General SDRAM (== the old allocLowSpeed).
void* alloc_sdram(std::size_t size, std::size_t align);
/// Non-stealable external SDRAM (== the old allocExternal; one heap now).
void* alloc_external(std::size_t size, std::size_t align);
/// Free a pointer from any of the above; routes to the owning heap by address.
void dealloc(void* ptr);
/// Usable bytes at `ptr` (routes by address).
std::size_t usable_size(void* ptr);

} // namespace deluge::memory
