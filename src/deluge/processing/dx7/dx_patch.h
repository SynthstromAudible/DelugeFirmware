/*
 * Copyright 2016-2017 Pascal Gauthier.
 * Copyright 2012 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "dsp-next/core/control.h"
#include "dsp-next/fixed/dx7/lfo.h"
#include "dsp-next/fixed/dx7/operator.h"
#include <cstdint>

class DxPatch {
public:
	DxPatch();

	uint8_t params[156];
	/// The engine the voices render in, chosen by setEngineMode.
	deluge::dsp::fixed::dx7::Engine engine;
	bool opSwitch(int op) const { return (params[155] >> op) & 1; }
	void setOpSwitch(int op, bool on) { params[155] = (params[155] & ~(1 << op)) | (on ? 1 : 0) * (1 << op); }
	uint8_t engineMode;

	// extra parameters
	int32_t random_detune;

	// TODO: these should be per voice with MPE
	// int amp_mod;
	int pitch_mod;
	int eg_mod;

	/// The patch's LFO, shared by its voices; configured from the patch before each block.
	deluge::dsp::Control<deluge::dsp::fixed::dx7::Lfo> lfo;
	/// The LFO's value after the last block, Q24.
	int32_t lfo_value = 0;

	void computeLfo(int n);

	void setEngineMode(int mode);
	void updateEngineMode();
};
