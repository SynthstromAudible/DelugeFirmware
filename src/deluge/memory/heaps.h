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

// The application's allocator heaps, built over the BSP's memory regions
// (deluge_memory_region) and backed by the Rust deluge_alloc core. These free
// functions are the clean way to reach a heap — the typed C++ allocators
// (fast/sdram/external_allocator) and the GeneralMemoryAllocator forwarders
// allocate from here, rather than each owning/locating heaps via
// GeneralMemoryAllocator::get(). The GMA is being reduced to the stealable/
// reclaim coordinator; heap ownership lives here.
//
// Only meaningful under DELUGE_USE_RUST_ALLOC; otherwise these return nullptr and
// the legacy MemoryRegion path (inside the GMA) is used. DelugeHeap is opaque
// here (full C ABI in libdeluge/alloc.h) so consumers don't need include/.

struct DelugeHeap;

namespace deluge::memory {

/// Build the heaps from deluge_memory_region() if not already built. Idempotent;
/// safe to call from any heap accessor.
void init_heaps();

/// Fast internal SRAM heap (DELUGE_MEM_FAST_INTERNAL region). nullptr if the
/// board has no such region or DELUGE_USE_RUST_ALLOC is off.
DelugeHeap* sram_heap();

/// Large external SDRAM heap (DELUGE_MEM_LARGE_EXTERNAL region). nullptr until the
/// SDRAM strangle wires it (and off without DELUGE_USE_RUST_ALLOC).
DelugeHeap* sdram_heap();

} // namespace deluge::memory
