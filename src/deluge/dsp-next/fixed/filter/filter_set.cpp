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

#include "dsp-next/fixed/filter/filter_set.h"
#include "dsp-next/core/wrapping.h"

#include "util/fixedpoint.h"

#include <algorithm>
#include <cstdint>
#include <memory>

namespace deluge::dsp::fixed {

namespace {

/// @brief Whether two high-pass slots run the same mode (the SVF's mode counting only for an SVF).
[[nodiscard]] bool same_mode(HighPassSlot a, HighPassSlot b) {
	return a.kind == b.kind && (a.kind != HighPassSlot::Kind::SVF || a.svf == b.svf);
}

/// @brief Restarts @p fade from dry when @p from_off: a filter switched on from off fades in.
template <typename Fade>
void fade_in_if(Fade& fade, bool from_off) {
	if (from_off) {
		fade.template on<&FadeIn::begin>();
	}
}

/// @brief @p filter over a mono @p block, its left channel alone; @p fade blended in over it while active.
template <typename Filter, typename Fade>
void render_mono(Filter& filter, Fade& fade, std::span<q31> block, FadeScratch scratch) {
	const bool fading = FadeIn::active(fade.state());
	std::span<q31> dry;
	if (fading) {
		dry = scratch.first(block.size());
		std::ranges::copy(block, dry.begin());
	}
	filter.renderLane(0, block, block);
	if (fading) {
		fade.renderBlock(dry, block, block);
	}
}

/// @brief @p filter over an interleaved L/R block; @p fade blended in over its samples, left then right, while
/// active.
template <typename Filter, typename Fade>
void render_stereo(Filter& filter, Fade& fade, std::span<q31> interleaved, FadeScratch scratch) {
	const bool fading = FadeIn::active(fade.state());
	std::span<q31> dry;
	if (fading) {
		dry = scratch.first(interleaved.size());
		std::ranges::copy(interleaved, dry.begin());
	}
	const auto frames = as_frames(interleaved);
	filter.renderBlock(frames, frames);
	if (fading) {
		fade.renderBlock(dry, interleaved, interleaved);
	}
}

} // namespace

FilterSet::View FilterSet::view() const {
	const bool low_svf = low_held_ == LowPassSlot::Kind::SVF;
	const bool high_svf = high_held_ == HighPassSlot::Kind::SVF;
	return {
	    .low_held = low_held_,
	    .low_ladder = low_svf ? nullptr : &low_pass_.ladder,
	    .low_svf = low_svf ? &low_pass_.svf : nullptr,
	    .high_held = high_held_,
	    .high_ladder = high_svf ? nullptr : &high_pass_.ladder,
	    .high_svf = high_svf ? &high_pass_.svf : nullptr,
	    .low_slot = low_slot_,
	    .high_slot = high_slot_,
	    .low_last = low_last_,
	    .high_last = high_last_,
	    .route = route_,
	    .low_fade = low_fade_.state(),
	    .high_fade = high_fade_.state(),
	};
}

void FilterSet::reset() {
	std::construct_at(&low_pass_.ladder);
	std::construct_at(&high_pass_.ladder);
	low_fade_.on<&FadeIn::zero>();
	high_fade_.on<&FadeIn::zero>();
	low_held_ = LowPassSlot::Kind::LADDER;
	high_held_ = HighPassSlot::Kind::LADDER;
	low_slot_ = {};
	high_slot_ = {};
	low_last_ = LowPassSlot::Kind::LADDER;
	high_last_ = {.kind = HighPassSlot::Kind::LADDER};
	route_ = FilterRoute::HIGH_TO_LOW;
}

void FilterSet::holdLowPass(LowPassSlot::Kind kind) {
	if (kind == low_held_) {
		return;
	}
	if (kind == LowPassSlot::Kind::SVF) {
		std::construct_at(&low_pass_.svf);
	}
	else {
		std::construct_at(&low_pass_.ladder);
	}
	low_held_ = kind;
}

void FilterSet::holdHighPass(HighPassSlot::Kind kind) {
	if (kind == high_held_) {
		return;
	}
	if (kind == HighPassSlot::Kind::SVF) {
		std::construct_at(&high_pass_.svf);
	}
	else {
		std::construct_at(&high_pass_.ladder);
	}
	high_held_ = kind;
}

q31 FilterSet::configure(q31 gain, const FilterConfig& low, LowPassSlot low_slot, const FilterConfig& high,
                         HighPassSlot high_slot, FilterRoute route) {
	low_slot_ = low_slot;
	high_slot_ = high_slot;
	route_ = route;

	if (low_slot.kind != LowPassSlot::Kind::OFF) {
		holdLowPass(low_slot.kind);
		const bool family_changed = low_last_ != low_slot.kind;
		const bool from_off = low_last_ == LowPassSlot::Kind::OFF;
		if (family_changed) {
			if (low_slot.kind == LowPassSlot::Kind::SVF) {
				low_pass_.svf.reset();
			}
			else {
				low_pass_.ladder.reset();
			}
			fade_in_if(low_fade_, from_off);
		}
		if (low_slot.kind == LowPassSlot::Kind::SVF) {
			low_pass_.svf.configure({.gain = gain, .config = low, .mode = low_slot.svf});
			gain = Svf::gain(low_pass_.svf.coefficients());
		}
		else {
			low_pass_.ladder.configure({.gain = gain, .config = low, .mode = low_slot.ladder});
			gain = LowPassLadder<CongNoise>::gain(low_pass_.ladder.coefficients());
		}
	}
	low_last_ = low_slot.kind;

	// At 50% resonance this keeps the level the filters had before their resonance compensation.
	gain = multiply_32x32_rshift32(gain, 1720000000) << 1;

	if (high_slot.kind != HighPassSlot::Kind::OFF) {
		holdHighPass(high_slot.kind);
		FilterConfig config = high;
		// Changes in the resonance's low bits, made too often, rustle.
		config.resonance = (high.resonance >> 21) << 21;
		const bool mode_changed = !same_mode(high_last_, high_slot);
		const bool from_off = high_last_.kind == HighPassSlot::Kind::OFF;
		if (high_slot.kind == HighPassSlot::Kind::LADDER) {
			high_pass_.ladder.configure({.gain = gain, .config = config});
			gain = HighPassLadder::gain(high_pass_.ladder.coefficients());
			if (mode_changed) {
				high_pass_.ladder.reset();
			}
		}
		else {
			// The morph inverted, so the high-pass SVF sweeps high → band or notch → low.
			config.morph = ((1 << 29) - 1) - high.morph;
			high_pass_.svf.configure({.gain = gain, .config = config, .mode = high_slot.svf});
			gain = Svf::gain(high_pass_.svf.coefficients());
			if (mode_changed) {
				high_pass_.svf.reset();
			}
		}
		if (mode_changed) {
			fade_in_if(high_fade_, from_off);
		}
	}
	high_last_ = high_slot;

	return gain;
}

void FilterSet::renderLowPassMono(std::span<q31> block, FadeScratch scratch) {
	switch (low_slot_.kind) {
	case LowPassSlot::Kind::LADDER:
		render_mono(low_pass_.ladder, low_fade_, block, scratch);
		break;
	case LowPassSlot::Kind::SVF:
		render_mono(low_pass_.svf, low_fade_, block, scratch);
		break;
	case LowPassSlot::Kind::OFF:
		break;
	}
}

void FilterSet::renderHighPassMono(std::span<q31> block, FadeScratch scratch) {
	switch (high_slot_.kind) {
	case HighPassSlot::Kind::LADDER:
		render_mono(high_pass_.ladder, high_fade_, block, scratch);
		break;
	case HighPassSlot::Kind::SVF:
		render_mono(high_pass_.svf, high_fade_, block, scratch);
		break;
	case HighPassSlot::Kind::OFF:
		break;
	}
}

void FilterSet::renderLowPassStereo(std::span<q31> interleaved, FadeScratch scratch) {
	switch (low_slot_.kind) {
	case LowPassSlot::Kind::LADDER:
		render_stereo(low_pass_.ladder, low_fade_, interleaved, scratch);
		break;
	case LowPassSlot::Kind::SVF:
		render_stereo(low_pass_.svf, low_fade_, interleaved, scratch);
		break;
	case LowPassSlot::Kind::OFF:
		break;
	}
}

void FilterSet::renderHighPassStereo(std::span<q31> interleaved, FadeScratch scratch) {
	switch (high_slot_.kind) {
	case HighPassSlot::Kind::LADDER:
		render_stereo(high_pass_.ladder, high_fade_, interleaved, scratch);
		break;
	case HighPassSlot::Kind::SVF:
		render_stereo(high_pass_.svf, high_fade_, interleaved, scratch);
		break;
	case HighPassSlot::Kind::OFF:
		break;
	}
}

namespace {

/// @brief @p block += @p other, sample by sample, wrapping on overflow (two's complement, as the hardware adds).
void add_wrapping(std::span<q31> block, std::span<const q31> other) {
	for (std::size_t i = 0; i < block.size(); i++) {
		block[i] = wrapping_add(block[i], other[i]);
	}
}

} // namespace

[[gnu::hot]] void FilterSet::renderMono(std::span<q31> block, FilterScratch scratch) {
	switch (route_) {
	case FilterRoute::HIGH_TO_LOW:
		renderHighPassMono(block, scratch.fade);
		renderLowPassMono(block, scratch.fade);
		break;
	case FilterRoute::LOW_TO_HIGH:
		renderLowPassMono(block, scratch.fade);
		renderHighPassMono(block, scratch.fade);
		break;
	case FilterRoute::PARALLEL: {
		const std::span<q31> copy = scratch.parallel.first(block.size());
		std::ranges::copy(block, copy.begin());
		renderHighPassMono(copy, scratch.fade);
		renderLowPassMono(block, scratch.fade);
		add_wrapping(block, copy);
		break;
	}
	}
}

[[gnu::hot]] void FilterSet::renderStereo(std::span<q31> interleaved, FilterScratch scratch) {
	switch (route_) {
	case FilterRoute::HIGH_TO_LOW:
		renderHighPassStereo(interleaved, scratch.fade);
		renderLowPassStereo(interleaved, scratch.fade);
		break;
	case FilterRoute::LOW_TO_HIGH:
		renderLowPassStereo(interleaved, scratch.fade);
		renderHighPassStereo(interleaved, scratch.fade);
		break;
	case FilterRoute::PARALLEL: {
		const std::span<q31> copy = scratch.parallel.first(interleaved.size());
		std::ranges::copy(interleaved, copy.begin());
		renderHighPassStereo(copy, scratch.fade);
		renderLowPassStereo(interleaved, scratch.fade);
		add_wrapping(interleaved, copy);
		break;
	}
	}
}

} // namespace deluge::dsp::fixed
