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

#include "model/mod_controllable/delay_arena_app.h"

#include "libdeluge/alloc.h"
#include "libdeluge/system.h"
#include "memory/memory_allocator_interface.h"
#include "runtime/app_loop.h"
#include "util/structural_edit_probe.h"

#include <atomic>

namespace deluge::model {
namespace {

deluge::runtime::JobId g_job = deluge::runtime::kNoJob;
std::atomic<uint32_t> g_runs{0};

void* lendRegion(std::size_t bytes) {
	return allocLowSpeed(static_cast<uint32_t>(bytes));
}
void unlendRegion(void* region) {
	delugeDealloc(region);
}
void* createHeap(void* base, std::size_t bytes) {
	return deluge_heap_create(base, bytes);
}
void* heapAlloc(void* heap, std::size_t bytes) {
	return deluge_alloc(static_cast<DelugeHeap*>(heap), bytes, 16);
}
void heapFree(void* heap, void* block) {
	deluge_free(static_cast<DelugeHeap*>(heap), block);
}
void requestMaintenance() {
	if (g_job != deluge::runtime::kNoJob) {
		deluge::runtime::appLoop.request(g_job);
	}
}

} // namespace

void maintainDelayArena() {
	deluge::effects::DelayArena& arena = delayArena();
	// One region a pass, until the margin holds: a load can reserve several largest tapes at once.
	for (std::size_t pass = 0; pass < deluge::effects::kArenaMaxRegions; pass++) {
		const std::size_t regionsBefore = arena.regions();
		deluge::effects::ArenaChange change = arena.prepare(0);
		const bool lent = change.lent != nullptr;
		{
			AudioExcludedSection section; // the render takes from these regions
			arena.commit(change);
			deluge::noteStructuralEdit(deluge::StructuralContainer::DELAY_TAPE);
		}
		arena.finish(change);
		if (!lent || arena.regions() <= regionsBefore || arena.margin_holds()) {
			break;
		}
	}
	g_runs.fetch_add(1, std::memory_order_relaxed);
}

deluge::effects::DelayArena& delayArena() {
	static deluge::effects::DelayArena arena{
	    {lendRegion, unlendRegion, createHeap, heapAlloc, heapFree, requestMaintenance}};
	return arena;
}

void registerDelayArenaJob() {
	g_job = deluge::runtime::appLoop.add("delay arena", &maintainDelayArena, deluge::runtime::kNever, 0);
}

uint32_t delayArenaMaintenanceRuns() {
	return g_runs.load(std::memory_order_relaxed);
}

} // namespace deluge::model
