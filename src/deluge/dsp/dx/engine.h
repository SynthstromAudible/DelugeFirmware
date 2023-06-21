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

#pragma once

#include "EngineMkI.h"
#include "definitions_cxx.hpp"
#include "dx7note.h"

// seems to cause crashes... maybe YAGNI
// #define DX_PREALLOC

class DxEngine {
public:
	DxEngine();
#define EXP2_LG_N_SAMPLES 10
#define EXP2_N_SAMPLES (1 << EXP2_LG_N_SAMPLES)
	int32_t exp2tab[EXP2_N_SAMPLES << 1];

#define TANH_LG_N_SAMPLES 10
#define TANH_N_SAMPLES (1 << TANH_LG_N_SAMPLES)
	int32_t tanhtab[TANH_N_SAMPLES << 1];

// Use twice as much RAM for the LUT but avoid a little computation
#define SIN_DELTA

#define SIN_LG_N_SAMPLES 10
#define SIN_N_SAMPLES (1 << SIN_LG_N_SAMPLES)
#ifdef SIN_DELTA
	int32_t sintab[SIN_N_SAMPLES << 1];
#else
	int32_t sintab[SIN_N_SAMPLES + 1];
#endif

#define FREQ_LG_N_SAMPLES 10
#define FREQ_N_SAMPLES (1 << FREQ_LG_N_SAMPLES)
	int32_t freq_lut[FREQ_N_SAMPLES + 1];

#ifdef DX_PREALLOC
	DxVoice dxVoices[kNumVoiceSamplesStatic];
	DxVoice* firstUnassignedDxVoice;
#endif

	EngineMkI engineMkI;
	FmCore engineModern;

	DxVoice* solicitDxVoice();
	void dxVoiceUnassigned(DxVoice* dxVoice);
	DxPatch* newPatch();
};

extern DxEngine* dxEngine;

DxEngine* getDxEngine();
