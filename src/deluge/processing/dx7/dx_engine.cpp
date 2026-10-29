/*
 * Copyright © 2015-2024 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"

#include "memory/fast_allocator.h"
#include "memory/memory_allocator_interface.h"
#include "memory/object_pool.h"
#include "processing/dx7/dx_engine.h"
#include "processing/dx7/dx_patch.h"
#include <cstdint>
#include <new>

DxEngine* dxEngine = nullptr;

using DxVoicePool = deluge::memory::ObjectPool<DxPoolVoice, deluge::memory::fast_allocator>;

static void init_engine() {
	// get ourselves an aligned pointer to use placement new with
	// todo: this should be a part of the allocator but not sure what the api should look like, easy bugfix for now
	void* engineMem = allocMaxSpeed(sizeof(DxEngine) + alignof(DxEngine) - 1);
	const auto address = reinterpret_cast<std::uintptr_t>(engineMem);
	engineMem = reinterpret_cast<void*>((address + alignof(DxEngine) - 1) & ~(std::uintptr_t{alignof(DxEngine)} - 1));
	dxEngine = new (engineMem) DxEngine();

	deluge::dsp::fixed::dx7::generate(dxEngine->tables);
}

gsl::owner<DxPoolVoice*> DxEngine::solicitDxVoice() {
	try {
		return DxVoicePool::get().acquire().release();
	} catch (const deluge::exception&) {
		return nullptr;
	}
}

void DxEngine::dxVoiceUnassigned(gsl::owner<DxPoolVoice*> dxVoice) {
	DxVoicePool::recycle(dxVoice);
}

DxPatch* DxEngine::newPatch() {
	void* memory = allocLowSpeed(sizeof(DxPatch));
	return new (memory) DxPatch;
}

// TODO: static initialization should be allowed.
DxEngine* getDxEngine() {
	if (dxEngine == nullptr) {
		init_engine();
	}
	return dxEngine;
}
