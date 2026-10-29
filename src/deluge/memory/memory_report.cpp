/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "memory/memory_report.h"
#include "io/debug/log.h"
#include "libdeluge/alloc.h"
#include "memory/heaps.h"
#include <cstddef>

// NOLINTBEGIN — addresses are only meaningful via &symbol
extern uint32_t program_stack_start;
extern uint32_t program_stack_end;
// NOLINTEND

namespace deluge::memory {
namespace {

/// Bytes of [start, end) below `end` that the stack has reached: everything above its lowest unpainted word.
uint32_t stackHighWater(const uint32_t* start, const uint32_t* end) {
	const volatile uint32_t* p = start;
	while (p < end && *p == kStackPaint) {
		++p;
	}
	return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(end) - reinterpret_cast<uintptr_t>(p));
}

struct HeapNumbers {
	size_t pool = 0;
	size_t free = 0;
	size_t minFree = 0;
};

HeapNumbers numbers(DelugeHeap* heap) {
	HeapNumbers n;
	(void)deluge_heap_stats(heap, &n.pool, &n.free, &n.minFree);
	return n;
}

} // namespace

void reportMemoryIfPainted() {
	// A build with no stack region of its own (the host sim) leaves the two symbols equal.
	if (&program_stack_start == &program_stack_end || program_stack_start != kStackPaint) {
		return;
	}
	const HeapNumbers sram = numbers(sram_heap());
	const HeapNumbers frunk = numbers(frunk_heap());
	const HeapNumbers sdram = numbers(sdram_heap());
	// Keys short enough that the line fits D_PRINTLN's 256 bytes: `_min` is the free bytes' low-water mark, `_hw` the
	// stack's high-water mark, and a bare name the region's size.
	D_PRINTLN(
	    "MEM sram=%u sram_free=%u sram_min=%u frunk=%u frunk_free=%u frunk_min=%u sdram_free=%u sdram_min=%u "
	    "stack=%u stack_hw=%u",
	    (unsigned)sram.pool, (unsigned)sram.free, (unsigned)sram.minFree, (unsigned)frunk.pool, (unsigned)frunk.free,
	    (unsigned)frunk.minFree, (unsigned)sdram.free, (unsigned)sdram.minFree,
	    (unsigned)(reinterpret_cast<uintptr_t>(&program_stack_end) - reinterpret_cast<uintptr_t>(&program_stack_start)),
	    (unsigned)stackHighWater(&program_stack_start, &program_stack_end));
}

} // namespace deluge::memory
