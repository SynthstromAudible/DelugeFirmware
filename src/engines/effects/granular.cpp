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

void Granular::destroy_state(RingAllocator allocator, void* state) {
	std::destroy_at(static_cast<Mono*>(state));
	allocator.release(state);
}

void Granular::idle(Kernel::State& state) {
	state.wraps_to_shutdown = -1;
}

Granular::Granular(RingAllocator allocator, EnterGrain enter) : allocator_(allocator), enter_(enter) {
}

Granular::~Granular() {
	if (ring_ != nullptr) {
		allocator_.release(ring_);
	}
	if (mono_ != nullptr) {
		destroy_state(allocator_, mono_);
	}
}

GranularBlockChange Granular::prepare(ModFXType type) {
	GranularBlockChange change;
	if (type != ModFXType::GRAIN) {
		return change;
	}
	if (mono_ == nullptr) {
		void* state = allocator_.allocate(sizeof(Mono));
		if (state == nullptr) {
			return change;
		}
		change.acquired_state = new (state) Mono;
	}
	if (ring_ == nullptr) {
		constexpr std::size_t kBytes = Kernel::kRingFrames * sizeof(dsp::StereoSample);
		void* ring = allocator_.allocate(kBytes);
		if (ring != nullptr) {
			std::memset(ring, 0, kBytes);
			change.acquired_ring = ring;
		}
	}
	return change;
}

void Granular::commit(ModFXType type, GranularBlockChange& change) {
	if (change.acquired_state != nullptr && mono_ == nullptr) {
		mono_ = static_cast<Mono*>(std::exchange(change.acquired_state, nullptr));
		if (enter_ == EnterGrain::KEEP) {
			mono_->on<&Granular::idle>();
		}
	}
	if (change.acquired_ring != nullptr) {
		if (ring_ != nullptr || mono_ == nullptr) {
			change.released_ring = std::exchange(change.acquired_ring, nullptr);
		}
		else {
			ring_ = static_cast<dsp::StereoSample*>(std::exchange(change.acquired_ring, nullptr));
			mono_->on<&Kernel::set_ring>(ring_);
			if (enter_ == EnterGrain::CLEAR) {
				mono_->on<&Kernel::clear>();
			}
		}
	}
	else if (type != ModFXType::GRAIN && ring_ != nullptr) {
		mono_->on<&Kernel::restart>();
		mono_->on<&Kernel::clear_ring>();
		change.released_ring = std::exchange(ring_, nullptr);
	}
}

void Granular::finish(GranularBlockChange& change) {
	if (change.released_ring != nullptr) {
		allocator_.release(std::exchange(change.released_ring, nullptr));
	}
	if (change.acquired_ring != nullptr) {
		allocator_.release(std::exchange(change.acquired_ring, nullptr));
	}
	if (change.acquired_state != nullptr) {
		destroy_state(allocator_, std::exchange(change.acquired_state, nullptr));
	}
}

void Granular::render(std::span<dsp::StereoSample> block, const GranularInputs& in, int32_t& post_fx_volume,
                      std::span<dsp::q31> reverb_send) {
	if (mono_ == nullptr || ring_ == nullptr) {
		return;
	}
	Mono& mono = *mono_;
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
	if (mono_ != nullptr) {
		mono_->on<&Kernel::clear>();
	}
}

void Granular::startSkippingRendering() {
	if (mono_ != nullptr) {
		mono_->on<&Kernel::restart>();
	}
}

int32_t Granular::samplesToShutdown() const {
	return mono_ == nullptr ? 0 : mono_->state().wraps_to_shutdown * Kernel::kRingFrames;
}

const Granular::Kernel::State* Granular::state() const {
	return mono_ == nullptr ? nullptr : &mono_->state();
}

} // namespace deluge::effects
