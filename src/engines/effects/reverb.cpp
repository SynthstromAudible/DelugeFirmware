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

#include "engines/effects/reverb.h"

namespace deluge::effects {

Reverb::Reverb() {
	for (int32_t& word : memory_.freeverb) { // begins the Freeverb member's lifetime, zeroed
		word = 0;
	}
	freeverb_.configure({.room_size = settings_.room_size, .damping = settings_.damping, .width = settings_.width});
	freeverb_.on<&Freeverb::set_memory>(memory_.freeverb);
}

void Reverb::begin_change() {
	rebuilding_ = true;
}

void Reverb::prepare(ReverbModel model) {
	prepared_ = model;
	if (model == ReverbModel::FREEVERB) {
		for (int32_t& word : memory_.freeverb) {
			word = 0;
		}
		freeverb_.reset();
		freeverb_.on<&Freeverb::set_memory>(memory_.freeverb);
	}
	else {
		for (float& slot : memory_.plate) {
			slot = 0.f;
		}
		plate_.reset();
		plate_.on<&Plate::set_memory>(memory_.plate);
	}
	configure();
	if (model != ReverbModel::FREEVERB) {
		plate_.on<&Plate::start>();
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
		return; // a model change is under way: no reverb, as the rebuild always rendered
	}
	if (settings_dirty_) {
		configure();
	}
	switch (model_) {
	case ReverbModel::FREEVERB:
		freeverb_.on<&Freeverb::set_send>(send.data(), pan_l, pan_r);
		freeverb_.renderBlock(block, block);
		break;
	case ReverbModel::MUTABLE:
	case ReverbModel::DIGITAL:
		plate_.on<&Plate::set_send>(send.data(), pan_l, pan_r);
		plate_.renderBlock(block, block);
		break;
	}
}

} // namespace deluge::effects
