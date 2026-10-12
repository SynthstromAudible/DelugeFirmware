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

/// The job: the arena's prepare, commit and finish around a section that excludes the render, with no owner counted
/// in or out.
void maintain() {
	deluge::effects::DelayArena& arena = delayArena();
	deluge::effects::ArenaChange change = arena.prepare(0);
	{
		AudioExcludedSection section; // the render takes from these regions
		arena.commit(change);
		deluge::noteStructuralEdit(deluge::StructuralContainer::DELAY_TAPE);
	}
	arena.finish(change);
	g_runs.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

deluge::effects::DelayArena& delayArena() {
	static deluge::effects::DelayArena arena{
	    {lendRegion, unlendRegion, createHeap, heapAlloc, heapFree, requestMaintenance}};
	return arena;
}

void registerDelayArenaJob() {
	g_job = deluge::runtime::appLoop.add("delay arena", &maintain, deluge::runtime::kNever, 0);
}

uint32_t delayArenaMaintenanceRuns() {
	return g_runs.load(std::memory_order_relaxed);
}

} // namespace deluge::model
