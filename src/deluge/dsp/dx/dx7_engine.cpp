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
#include "dsp/dx/dx7_engine.h"
#include "dsp/dx/dx7note.h"
#include "dsp/dx/engine.h"
#include <cmath>

namespace deluge::dsp {

bool DX7Engine::noteOn(DxPatch& patch, int midinote, int velocity) {
	if (voice_ == nullptr) { // We might actually already have one, and just be restarting this voice
		voice_ = getDxEngine()->solicitDxVoice();
		if (voice_ == nullptr) {
			return false;
		}
	}
	voice_->init(patch, midinote, velocity);
	return true;
}

void DX7Engine::unassign() {
	if (voice_ != nullptr) {
		getDxEngine()->dxVoiceUnassigned(voice_);
		voice_ = nullptr;
	}
}

void DX7Engine::keyup() {
	if (voice_ != nullptr) {
		voice_->keyup();
	}
}

bool DX7Engine::render(int32_t* buf, int32_t numSamples, uint32_t phaseIncrement, DxPatch& patch, int32_t ampModParam,
                       bool patchChanged, int midinote) {
	// TODO: 1. use existing int log function?
	//       2. going from phase to logs (and then let MSFA turn those logs into phase again) is sus af
	//         rework MSFA to use our phase incerements directly?
	int logpitch = (int)(log2f(phaseIncrement) * (1 << 24));
	int adjpitch = logpitch - 278023814;

	DxVoiceCtrl ctrl{};
	ctrl.ampmod = ampModParam >> 13;
	// ctrl.ratemod = params.paramFinalValues[params::LOCAL_CARRIER_0_FEEDBACK + s] >> 16;
	if (patchChanged) {
		voice_->update(patch, midinote);
	}
	return voice_->compute(buf, numSamples, adjpitch, &patch, &ctrl);
}

} // namespace deluge::dsp
