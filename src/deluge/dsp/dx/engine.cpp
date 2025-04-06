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
#include "stdint.h"

#include "EngineMkI.h"
#include "dx7note.h"
#include "engine.h"
#include "math_lut.h"
#include "memory/memory_allocator_interface.h"
#include <new>

DxEngine* dxEngine = nullptr;

static void init_engine(void) {
	// get ourselves an aligned pointer to use placement new with
	// todo: this should be a part of the allocator but not sure what the api should look like, easy bugfix for now
	void* engineMem = allocMaxSpeed(sizeof(DxEngine) + alignof(DxEngine) - 1);
	engineMem = (void*)((((intptr_t)engineMem) + alignof(DxEngine) - 1) & -alignof(DxEngine));
	dxEngine = new (engineMem) DxEngine();

	dx_init_lut_data();

	PitchEnv::init(44100);
	Env::init_sr(44100);
}

DxEngine::DxEngine() {
#ifdef DX_PREALLOC
	firstUnassignedDxVoice = &dxVoices[0];

	for (int i = 0; i < kNumVoiceSamplesStatic - 1; i++) {
		dxVoices[i].nextUnassigned = &dxVoices[i + 1];
	}
#endif
}

DxVoice* DxEngine::solicitDxVoice() {
#ifdef DX_PREALLOC
	if (firstUnassignedDxVoice) {
		DxVoice* toReturn = firstUnassignedDxVoice;
		firstUnassignedDxVoice = firstUnassignedDxVoice->nextUnassigned;
		toReturn->preallocated = true;
		return toReturn;
	}
#endif
	void* memory = allocMaxSpeed(sizeof(DxVoice));
	if (!memory)
		return nullptr;

	return new (memory) DxVoice();
}

void DxEngine::dxVoiceUnassigned(DxVoice* dxVoice) {
#ifdef DX_PREALLOC
	if (dxVoice->preallocated) {
		dxVoice->nextUnassigned = firstUnassignedDxVoice;
		firstUnassignedDxVoice = dxVoice;
		return;
	}
#endif
	delugeDealloc(dxVoice);
}

DxPatch* DxEngine::newPatch(void) {
	void* memory = allocMaxSpeed(sizeof(DxPatch));
	return new (memory) DxPatch;
}

// TODO: static initialization should be allowed.
DxEngine* getDxEngine() {
	if (dxEngine == nullptr) {
		init_engine();
	}
	return dxEngine;
}
