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
#include "dsp-next/fixed/dx7/patch.h"
#include <cstdint>

class DxPatch {
public:
	DxPatch();

	/// The patch's bytes: six ops of 21 (op 6 first), the globals, then one op-switch bit per op in byte 155.
	deluge::dsp::fixed::dx7::PatchBytes params{};
	/// The bytes as the voices read them: params, clamped to the engine's ranges (dx7::sanitise). The voices read
	/// this copy every block instead of each taking its own; refreshVoiceParams() renews it at each note-on and in
	/// the block the model changes the patch (Source::dxPatchChanged, which every write through
	/// bridge::applyDxPatch raises).
	deluge::dsp::fixed::dx7::PatchBytes voiceParams{};
	/// Renews voiceParams from params.
	void refreshVoiceParams();
	/// The engine the voices render in, chosen by setEngineMode.
	deluge::dsp::fixed::dx7::Engine engine = deluge::dsp::fixed::dx7::Engine::MODERN;
	/// Whether op @p op is switched on (bit @p op of byte 155).
	[[nodiscard]] bool opSwitch(int op) const { return (params[155] >> op) & 1; }
	/// The engine mode as the patch stores it: 0 auto, 1 modern, 2 MkI.
	uint8_t engineMode = 0;

	/// The patch's random-detune amount.
	int32_t random_detune = 0;
	/// The envelope's amplitude-modulation floor, 0 to 127.
	int eg_mod = 127;

	/// The patch's LFO, shared by its voices; configured from the patch before each block.
	deluge::dsp::Control<deluge::dsp::fixed::dx7::Lfo> lfo;
	/// The LFO's value after the last block, Q24.
	int32_t lfo_value = 0;

	void computeLfo(int n);

	void setEngineMode(int mode);
	void updateEngineMode();
};
