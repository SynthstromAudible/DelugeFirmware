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

#include "engines/effects/mod_fx.h"

#include <cstring>
#include <utility>

namespace deluge::effects {

ModFx::ModFx(RingAllocator allocator) : allocator_(allocator) {
}

ModFx::~ModFx() {
	if (mono_.state().ring != nullptr) {
		allocator_.release(mono_.state().ring);
	}
}

RingChange ModFx::prepare(ModFXType type) {
	RingChange change;
	if (!is_ring_type(type) || holdsRing()) {
		return change;
	}
	constexpr std::size_t kBytes = Kernel::kRingFrames * sizeof(dsp::StereoSample);
	void* block = allocator_.allocate(kBytes);
	if (block != nullptr) {
		std::memset(block, 0, kBytes);
		change.acquired = static_cast<dsp::StereoSample*>(block);
	}
	return change;
}

void ModFx::commit(ModFXType type, RingChange& change) {
	if (change.acquired != nullptr) {
		if (holdsRing()) {
			change.released = std::exchange(change.acquired, nullptr);
		}
		else {
			mono_.on<&Kernel::set_ring>(std::exchange(change.acquired, nullptr));
			soft_off_ = false; // prepare zeroed it
		}
	}
	else if (!is_ring_type(type) && holdsRing()) {
		change.released = mono_.state().ring;
		mono_.on<&Kernel::clear_ring>();
	}
}

void ModFx::finish(RingChange& change) {
	if (change.released != nullptr) {
		allocator_.release(std::exchange(change.released, nullptr));
	}
	if (change.acquired != nullptr) {
		allocator_.release(std::exchange(change.acquired, nullptr));
	}
}

void ModFx::render(std::span<dsp::StereoSample> block, const ModFxInputs& in, int32_t& post_fx_volume) {
	const bool ring_type = is_ring_type(in.active);
	if (!ring_type) {
		soft_off_ = true;
	}
	else if (soft_off_) {
		mono_.on<&Kernel::zero_ring>();
		soft_off_ = false;
	}
	if (!in.sound || in.active == ModFXType::NONE || in.active == ModFXType::GRAIN) {
		return;
	}
	const Kernel::Settings settings{
	    .mode = mod_fx_mode(in.active, in.stereo),
	    .rate = in.rate,
	    .depth = in.depth,
	    .offset = in.offset,
	    .feedback = in.feedback,
	};
	if (!settings_ || *settings_ != settings) {
		mono_.configure(settings);
		settings_ = settings;
	}
	post_fx_volume = Kernel::post_fx_volume(mono_.coefficients(), post_fx_volume);
	if (ring_type && !holdsRing()) {
		return;
	}
	mono_.renderBlock(block, block);
}

void ModFx::tick(int32_t frames, int32_t rate) {
	mono_.on<&Kernel::tick>(static_cast<uint32_t>(frames), static_cast<uint32_t>(rate));
}

void ModFx::clearMemory() {
	if (soft_off_) {
		mono_.on<&Kernel::clear_phaser>();
		return;
	}
	mono_.on<&Kernel::clear_memory>();
}

} // namespace deluge::effects
