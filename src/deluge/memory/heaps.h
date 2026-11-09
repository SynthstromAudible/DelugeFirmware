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
#include <cstdint>

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

// Allocation helpers — the clean surface app code allocates from, so it needs
// neither GeneralMemoryAllocator::get() nor libdeluge/alloc.h. They allocate from
// the deluge_alloc heaps above; `align` is honored (default 16, the SIMD floor).
// Return nullptr on OOM. Under DELUGE_DETERMINISTIC_ALLOC (sim/golden only) the
// block is zeroed before return, matching the old GMA alloc path.

/// @brief [audio] Heap allocations made on the audio tier since boot (host-only diagnostic; always 0 on device).
///
/// @note The render must not allocate: it shares the heap with every other tier.
/// @return Allocation count since boot.
uint32_t audioTierAllocations();
/// @brief [task] Heap allocations made off the render inside a NoAllocationScope since boot (host-only diagnostic;
///        always 0 on device).
/// @return Allocation count since boot.
uint32_t maskedAllocations();
/// @brief Count an allocation if the render is making it, for audioTierAllocations().
///
/// Host-only; a no-op on the device.
void countRenderAllocation();

/// @brief Mark the caller as inside (or leaving) a render block: a render made outside the audio context (a host
/// harness thread, the offline export loop) is the render tier for the tier that entered it.
/// @param entering True when entering the render block, false when leaving it.
void noteRenderBlock(bool entering);
/// @brief Whether the caller is on the audio tier, or inside a render block its own tier entered.
/// @return True when inside a render block.
bool inRenderBlock();

/// @brief Mark the caller as inside (or leaving) a stretch that must not allocate: one that holds the render off, which
///        on the device masks interrupts for its length.
/// @param entering True when entering the stretch, false when leaving it.
void noteNoAllocationScope(bool entering);
/// @brief Whether the caller is inside a NoAllocationScope.
/// @return True when inside one.
bool inNoAllocationScope();

/// @brief A stretch that must not allocate; maskedAllocations() counts what it does.
struct NoAllocationScope {
	NoAllocationScope() { noteNoAllocationScope(true); }
	~NoAllocationScope() { noteNoAllocationScope(false); }
	NoAllocationScope(const NoAllocationScope&) = delete;
	NoAllocationScope& operator=(const NoAllocationScope&) = delete;
};

/// @brief A render block's extent: the caller is the render tier while it lives.
struct RenderBlock {
	RenderBlock() { noteRenderBlock(true); }
	~RenderBlock() { noteRenderBlock(false); }
	RenderBlock(const RenderBlock&) = delete;
	RenderBlock& operator=(const RenderBlock&) = delete;
};

/// @brief [task] [audio] [isr] Whether the caller is the audio render: the device's audio context, or a render block
/// (a host harness's, rendering on a thread deluge_in_audio_context() does not see, or an offline export block).
/// @return True on the render tier.
bool onRenderTier();

#ifdef DELUGE_HOST
/// @brief Host-only test seam: make the next allocation of exactly @p size bytes fail, once.
///        GeneralMemoryAllocator::alloc (delugeAlloc), alloc_fast and alloc_external honour it.
/// @param size Allocation size (bytes) to fail on its next occurrence.
void failNextAllocationOfSize(std::size_t size);

/// @brief [task] [audio] Consume a failure previously armed for @p size (host-only).
/// @param size Allocation size to check/consume.
/// @return True if a matching failure was pending (and is now consumed).
bool injectedAllocationFailure(std::size_t size);

/// @brief Whether a failure armed by failNextAllocationOfSize() has not yet been consumed (host-only).
/// @return True if a pending failure remains unconsumed.
bool allocationFailurePending();
#endif

/// SRAM-preferred (small allocs to the frunk heap) with SDRAM fallback (== the old
/// allocMaxSpeed).
void* alloc_fast(std::size_t size, std::size_t align = 16);
/// General SDRAM (== the old allocLowSpeed / allocExternal).
void* alloc_sdram(std::size_t size, std::size_t align = 16);
/// Non-stealable external SDRAM (== the old allocExternal; one heap now).
void* alloc_external(std::size_t size, std::size_t align = 16);
/// Free a pointer from any of the above; routes to the owning heap by address.
void dealloc(void* ptr);
/// Shrink a block in place, preserving the leading bytes; returns the new usable
/// size (== the old GeneralMemoryAllocator::shortenRight).
std::size_t shrink(void* ptr, std::size_t new_size);
/// Left-shrink is unsupported on the deluge_alloc heaps (realloc can't move the
/// payload left), so this is a no-op returning 0 — the amount actually shortened
/// (== the old GeneralMemoryAllocator::shortenLeft on the Rust heap). Kept so call
/// sites needn't special-case it.
std::size_t shrink_left(void* ptr, std::size_t amount_to_shorten, std::size_t num_bytes_to_move_right);
/// Usable bytes at `ptr` (routes by address).
std::size_t usable_size(void* ptr);

} // namespace deluge::memory
