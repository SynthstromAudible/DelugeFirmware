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

#include "processing/dx7/dx_patch.h"
#include "engines/deluge/dx7_init_voice.h"
#include "processing/dx7/dx_engine.h"
#include <cstddef>
#include <cstring>

DxPatch::DxPatch() {
	memcpy(params, deluge::instrument::kDx7InitVoice.data(), sizeof params);
	eg_mod = 127;
	random_detune = 0;

	setEngineMode(0);
}

void DxPatch::updateEngineMode() {
	using deluge::dsp::fixed::dx7::Engine;
	engine = Engine::MODERN;
	if (engineMode == 2) {
		engine = Engine::MKI;
	}
	else if (engineMode == 0) {
		int algo = params[134];
		int feedback = params[135];
		// only MkI implements the feedback loops of algo 4 and 6
		if (feedback > 0 && (algo == 3 || algo == 5)) {
			engine = Engine::MKI;
		}
	}
}

void DxPatch::setEngineMode(int mode) {
	engineMode = (uint8_t)mode;

	updateEngineMode();
}

void DxPatch::computeLfo(int n) {
	lfo.configure({params[137], params[142], &dxEngine->tables});
	lfo_value = lfo.advance(static_cast<std::size_t>(n));
}
