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

#include "effects/stutter.h"

namespace deluge::effects {

Stutter::Stutter(RingAllocator allocator) : allocator_{allocator} {
}

Stutter::~Stutter() {
	if (storage_ != nullptr) {
		allocator_.release(storage_);
	}
}

StutterChange Stutter::prepare(uint32_t rate) {
	const TapeShape shape = tape_shape(rate, true);
	return {.acquired = static_cast<dsp::StereoSample*>(allocator_.allocate(shape.frames * sizeof(dsp::StereoSample))),
	        .shape = shape,
	        .rate = rate,
	        .released = nullptr};
}

bool Stutter::commit(StutterChange& change, const StutterState& settings) {
	if (change.acquired == nullptr) {
		return false;
	}
	change.released = storage_;
	storage_ = change.acquired;
	change.acquired = nullptr;
	tape_ = make_tape({storage_, change.shape.frames}, change.shape, change.rate);
	mono_.reset();
	mono_.on<&Kernel::start>(static_cast<int32_t>(tape_.size()), settings.reversed, settings.ping_pong);
	return true;
}

void Stutter::commit_end(StutterChange& change) {
	change.released = storage_;
	storage_ = nullptr;
	tape_ = {};
	mono_.reset();
}

void Stutter::finish(StutterChange& change) {
	for (dsp::StereoSample** block : {&change.acquired, &change.released}) {
		if (*block != nullptr) {
			allocator_.release(*block);
			*block = nullptr;
		}
	}
}

void Stutter::render(std::span<dsp::StereoSample> block, uint32_t rate) {
	if (storage_ == nullptr) {
		return;
	}
	mono_.configure({.rate = rate, .tape = &tape_, .playing = mono_.state().playing});
	mono_.render_block(block, block);
	if (!mono_.state().playing && mono_.state().frames_left < 0) {
		mono_.on<&Kernel::finish_block>(tape_);
	}
}

} // namespace deluge::effects
