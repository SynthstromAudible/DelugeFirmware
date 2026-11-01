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

#include "effects/reverb.h"

namespace deluge::effects {

Reverb::Reverb() {
	zero_freeverb();
	freeverb_.configure({.room_size = settings_.room_size, .damping = settings_.damping, .width = settings_.width});
	freeverb_.on<&Freeverb::set_memory>(memory_.freeverb);
}

void Reverb::begin_change() {
	rebuilding_ = true;
	requested_.reset();
}

void Reverb::prepare(ReverbModel model) {
	prepared_ = model;
	if (model == ReverbModel::FREEVERB) {
		zero_freeverb();
		freeverb_.reset();
		freeverb_.on<&Freeverb::set_memory>(memory_.freeverb);
	}
	else {
		zero_plate();
		plate_.reset();
		plate_.on<&Plate::set_memory>(memory_.plate);
	}
	configure();
	if (model != ReverbModel::FREEVERB) {
		plate_.on<&Plate::start>();
	}
	// The render configures again at its first block: a publish that lands while this runs (the audio tier's, in a
	// launch-time song swap) is never lost to the configure above.
	settings_dirty_ = true;
}

void Reverb::zero_freeverb() {
	// Assigning each element through the member starts its lifetime as the union's active member.
	for (std::size_t i = 0; i < Freeverb::kMemoryWords; ++i) {
		memory_.freeverb[i] = 0;
	}
}

void Reverb::zero_plate() {
	// As zero_freeverb.
	for (std::size_t i = 0; i < Plate::kMemoryFloats; ++i) {
		memory_.plate[i] = 0.f;
	}
}

void Reverb::commit() {
	model_ = prepared_;
	rebuilding_ = false;
}

void Reverb::publish(const Settings& settings) {
	settings_ = settings;
	settings_dirty_ = true;
}

void Reverb::configure() {
	if (prepared_ == ReverbModel::FREEVERB) {
		freeverb_.configure({.room_size = settings_.room_size, .damping = settings_.damping, .width = settings_.width});
	}
	else {
		plate_.configure({.mode = prepared_ == ReverbModel::DIGITAL ? Plate::Mode::DIGITAL : Plate::Mode::MUTABLE,
		                  .room_size = settings_.room_size,
		                  .damping = settings_.damping,
		                  .width = settings_.width,
		                  .hpf = settings_.hpf,
		                  .lpf = settings_.lpf});
	}
	settings_dirty_ = false;
}

void Reverb::render(std::span<dsp::StereoSample> block, std::span<const int32_t> send, int32_t pan_l, int32_t pan_r) {
	if (rebuilding_) {
		return; // a model change is under way: the render adds no reverb until commit
	}
	if (settings_dirty_) {
		configure();
	}
	switch (model_) {
	case ReverbModel::FREEVERB:
		freeverb_.on<&Freeverb::set_send>(send.data(), pan_l, pan_r);
		freeverb_.render_block(block, block);
		break;
	case ReverbModel::MUTABLE:
	case ReverbModel::DIGITAL:
		plate_.on<&Plate::set_send>(send.data(), pan_l, pan_r);
		plate_.render_block(block, block);
		break;
	}
}

int32_t Reverb::return_level(uint32_t frames, int32_t hit, const SidechainState& state, uint32_t tick_inverse) {
	if (return_duck_.depth == 0) {
		return dsp::fixed::kDuckGainNeutral;
	}
	return return_sidechain_.level<dsp::fixed::DuckScale::REVERB_RETURN>({
	    .frames = frames,
	    .hit = hit,
	    .shape = return_duck_.shape,
	    .depth = return_duck_.depth,
	    .state = state,
	    .tick_inverse = tick_inverse,
	});
}

} // namespace deluge::effects
