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

#include "engines/effects/granular.h"

#include <cstring>
#include <memory>
#include <new>
#include <utility>

namespace deluge::effects {

struct Granular::Held {
	dsp::Mono<Kernel> mono;
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
	dsp::StereoSample ring[Kernel::kRingFrames];
};

void Granular::destroy(RingAllocator allocator, void* block) {
	std::destroy_at(static_cast<Held*>(block));
	allocator.release(block);
}

Granular::Granular(RingAllocator allocator) : allocator_(allocator) {
}

Granular::~Granular() {
	if (held_ != nullptr) {
		destroy(allocator_, held_);
	}
}

GranularBlockChange Granular::prepare(ModFXType type) {
	GranularBlockChange change;
	if (type != ModFXType::GRAIN || held_ != nullptr) {
		return change;
	}
	void* block = allocator_.allocate(sizeof(Held));
	if (block != nullptr) {
		auto* held = new (block) Held;
		std::memset(held->ring, 0, sizeof(held->ring));
		held->mono.on<&Kernel::set_ring>(static_cast<dsp::StereoSample*>(held->ring));
		change.acquired = held;
	}
	return change;
}

void Granular::commit(ModFXType type, GranularBlockChange& change) {
	if (change.acquired != nullptr) {
		if (held_ != nullptr) {
			change.released = std::exchange(change.acquired, nullptr);
		}
		else {
			held_ = static_cast<Held*>(std::exchange(change.acquired, nullptr));
		}
	}
	else if (type != ModFXType::GRAIN && held_ != nullptr) {
		change.released = std::exchange(held_, nullptr);
	}
}

void Granular::finish(GranularBlockChange& change) {
	if (change.released != nullptr) {
		destroy(allocator_, std::exchange(change.released, nullptr));
	}
	if (change.acquired != nullptr) {
		destroy(allocator_, std::exchange(change.acquired, nullptr));
	}
}

void Granular::render(std::span<dsp::StereoSample> block, const GranularInputs& in, int32_t& post_fx_volume,
                      std::span<dsp::q31> reverb_send) {
	if (held_ == nullptr) {
		return;
	}
	dsp::Mono<Kernel>& mono = held_->mono;
	if (in.sound || mono.state().wraps_to_shutdown >= 0) {
		if (in.sound) {
			mono.on<&Kernel::arm_shutdown>();
		}
		mono.configure({.rate = in.rate, .mix = in.mix, .density = in.density, .randomness = in.randomness});
		post_fx_volume = Kernel::post_fx_volume(post_fx_volume);
		mono.on<&Kernel::set_send>(reverb_send.data(), in.send_amount);
		mono.renderBlock(block, block);
		if (mono.state().wraps_to_shutdown < 0) {
			mono.on<&Kernel::restart>();
		}
	}
	mono.on<&Kernel::note_full>();
}

void Granular::clear() {
	if (held_ != nullptr) {
		held_->mono.on<&Kernel::clear>();
	}
}

void Granular::startSkippingRendering() {
	if (held_ != nullptr) {
		held_->mono.on<&Kernel::restart>();
	}
}

int32_t Granular::samplesToShutdown() const {
	return held_ == nullptr ? 0 : held_->mono.state().wraps_to_shutdown * Kernel::kRingFrames;
}

const Granular::Kernel::State* Granular::state() const {
	return held_ == nullptr ? nullptr : &held_->mono.state();
}

} // namespace deluge::effects
