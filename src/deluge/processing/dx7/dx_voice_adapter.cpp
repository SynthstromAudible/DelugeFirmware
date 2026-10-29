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
#include "processing/dx7/dx_voice_adapter.h"
#include "board_config.h" // CACHE_LINE_SIZE
#include "dsp-next/fixed/dx7/algorithms.h"
#include "processing/dx7/dx_engine.h"
#include "processing/dx7/dx_patch.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring> // memset

namespace deluge::dsp {

namespace {

using PoolVoice = fixed::dx7::Voice<fixed::CongNoise>;

/// @brief The voice's settings: a snapshot of @p patch, its bytes clamped to the engine's ranges, the note and the
/// engine's tables.
PoolVoice::Settings settings_of(const DxPatch& patch, int midinote, int velocity) {
	PoolVoice::Settings s{.patch = patch.params,
	                      .midinote = midinote,
	                      .velocity = velocity,
	                      .random_detune = patch.random_detune,
	                      .engine = patch.engine,
	                      .tables = &getDxEngine()->tables};
	fixed::dx7::sanitise(s.patch);
	return s;
}

} // namespace

bool DxVoiceAdapter::noteOn(void* patch, int midinote, int velocity) {
	if (voice_ == nullptr) { // We might actually already have one, and just be restarting this voice
		voice_ = getDxEngine()->solicitDxVoice();
		if (voice_ == nullptr) {
			return false;
		}
	}
	patch_ = static_cast<DxPatch*>(patch);
	midinote_ = midinote;
	velocity_ = velocity;
	const PoolVoice::Settings s = settings_of(*patch_, midinote_, velocity_);
	voice_->start(s);
	if (PoolVoice::key_sync_lfo(s)) {
		patch_->lfo.on<&fixed::dx7::Lfo::key_sync>();
	}
	return true;
}

void DxVoiceAdapter::unassign() {
	if (voice_ != nullptr) {
		getDxEngine()->dxVoiceUnassigned(voice_);
		voice_ = nullptr;
	}
	patch_ = nullptr;
}

void DxVoiceAdapter::keyup() {
	if (voice_ != nullptr) {
		voice_->release(settings_of(*patch_, midinote_, velocity_));
	}
}

std::span<const int32_t> DxVoiceAdapter::render(int32_t numSamples, uint32_t phaseIncrement, int32_t ampModParam,
                                                bool patchChanged, int midinote) {
	static int32_t uniBuf[fixed::dx7::kMaxBlock] __attribute__((aligned(CACHE_LINE_SIZE)));
	memset(uniBuf, 0, sizeof uniBuf);

	// TODO: 1. use existing int log function?
	//       2. going from phase to logs (and then the voice's frequency table turning those logs into phase again) is
	//         a round trip: take our phase increments directly?
	int logpitch = (int)(log2f(phaseIncrement) * (1 << 24));
	int adjpitch = logpitch - 278023814;

	midinote_ = midinote;
	const PoolVoice::Settings s = settings_of(*patch_, midinote_, velocity_);
	const PoolVoice::BlockInputs in{
	    .base_pitch = adjpitch, .lfo_value = patch_->lfo_value, .eg_mod = patch_->eg_mod, .amp_mod = ampModParam >> 13};
	if (patchChanged) {
		voice_->retune(s);
	}
	const auto n = static_cast<std::size_t>(numSamples);
	if (!voice_->renderBlock(s, {uniBuf, n}, in, getDxEngine()->buses())) {
		return {}; // voice has ended
	}
	return std::span<const int32_t>{uniBuf, n};
}

} // namespace deluge::dsp
