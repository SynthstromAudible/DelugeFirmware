/*
 * Copyright © 2015-2026 Synthstrom Audible Limited
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

#include "dsp-next/fixed/delay/delay_processor.h"

#include "dsp-next/fixed/shaping/tanh.h"
#include "util/fixedpoint.h"

#include <algorithm>
#include <cstddef>
#include <ranges>

namespace deluge::dsp::fixed {

DelayBlockResult DelayProcessor::process(std::span<StereoSample> block, DelayTape* primary, DelayTape* secondary,
                                         const DelayBlock& settings, std::span<StereoSample> scratch) {
	const bool reading = primary != nullptr && primary->isActive();
	const bool growing = secondary != nullptr && secondary->isActive();

	if (primary != nullptr) {
		primary->setupForRender(settings.rate);
	}
	if (growing) {
		secondary->setupForRender(settings.rate);
	}

	const std::span<StereoSample> work = scratch.first(block.size());

	DelayBlockResult result{.wrapped = false, .secondary_written = 0};
	DelayTape::Head head{};
	if (reading) {
		head = primary->head();
		result.wrapped = read(*primary, work);
	}
	else {
		std::ranges::fill(work, StereoSample{.l = 0, .r = 0});
	}

	shapeFeedback(work, settings);
	highPass(work);
	mix(work, block, settings.ping_pong && settings.render_in_stereo);

	if (reading) {
		writePrimary(*primary, head, work);
	}
	if (growing) {
		// The secondary tape's wraps replace the primary's: it is the tape that will play on.
		result = writeSecondary(*secondary, work);
	}
	return result;
}

bool DelayProcessor::read(DelayTape& primary, std::span<StereoSample> work) {
	bool wrapped = false;
	if (primary.isNative()) {
		for (StereoSample& sample : work) {
			wrapped = primary.clearAndMoveOn() || wrapped;
			sample = primary.current();
		}
		return wrapped;
	}

	for (StereoSample& sample : work) {
		// 16.16 strengths of the sample under the head and the next.
		const int32_t strength2 = primary.advance([&] { wrapped = primary.clearAndMoveOn() || wrapped; });
		const int32_t strength1 = 65536 - strength2;

		StereoSample* next = &primary.current() + 1;
		if (next == primary.end()) {
			next = primary.begin();
		}
		const StereoSample from1 = primary.current();
		const StereoSample from2 = *next;

		sample.l =
		    (multiply_32x32_rshift32(from1.l, strength1 << 14) + multiply_32x32_rshift32(from2.l, strength2 << 14))
		    << 2;
		sample.r =
		    (multiply_32x32_rshift32(from1.r, strength1 << 14) + multiply_32x32_rshift32(from2.r, strength2 << 14))
		    << 2;
	}
	return wrapped;
}

void DelayProcessor::shapeFeedback(std::span<StereoSample> work, const DelayBlock& settings) {
	if (settings.analog) {
		for (StereoSample& sample : work) {
			sample = analog_ir_.process(sample);
		}
		// Less headroom than digital: the analog saturation sounds fine driven.
		const auto drive = static_cast<uint32_t>(settings.analog_saturation);
		for (StereoSample& sample : work) {
			sample.l = tanh(multiply_32x32_rshift32(sample.l, settings.feedback), drive) << 2;
			sample.r = tanh(multiply_32x32_rshift32(sample.r, settings.feedback), drive) << 2;
		}
		return;
	}

	// More headroom than analog: a pure digital clip sounds bad.
	for (StereoSample& sample : work) {
		sample.l = signed_saturate<32 - 3>(multiply_32x32_rshift32(sample.l, settings.feedback)) << 2;
		sample.r = signed_saturate<32 - 3>(multiply_32x32_rshift32(sample.r, settings.feedback)) << 2;
	}
}

void DelayProcessor::highPass(std::span<StereoSample> work) {
	for (StereoSample& sample : work) {
		high_pass_l_ += (sample.l - high_pass_l_) >> 11;
		sample.l -= high_pass_l_;

		high_pass_r_ += (sample.r - high_pass_r_) >> 11;
		sample.r -= high_pass_r_;
	}
}

void DelayProcessor::mix(std::span<StereoSample> work, std::span<StereoSample> block, bool cross) {
	for (auto [feedback, output] : std::views::zip(work, block)) {
		const StereoSample delayed = feedback;
		if (cross) {
			feedback.l = delayed.r;
			feedback.r = ((output.l + output.r) >> 1) + delayed.l;
		}
		else {
			feedback += output;
		}
		output += delayed;
	}
}

void DelayProcessor::writePrimary(DelayTape& primary, DelayTape::Head head, std::span<const StereoSample> work) {
	if (primary.isNative()) {
		auto index = (head.pos - primary.begin()) - DelayTape::kWriteLag;
		if (index < 0) {
			index += static_cast<std::ptrdiff_t>(primary.sizeIncludingExtra());
		}
		StereoSample* write_pos = primary.begin() + index;
		for (const StereoSample sample : work) {
			primary.writeNativeAndMoveOn(sample, write_pos);
		}
		return;
	}

	primary.setHead(head);
	for (const StereoSample sample : work) {
		const int32_t strength2 = primary.advance([&] { primary.moveOn(); });
		const int32_t strength1 = 65536 - strength2;
		primary.writeResampled(sample, strength1, strength2);
	}
}

DelayBlockResult DelayProcessor::writeSecondary(DelayTape& secondary, std::span<const StereoSample> work) {
	DelayBlockResult result{.wrapped = false, .secondary_written = 0};
	auto step = [&] {
		result.wrapped = secondary.clearAndMoveOn() || result.wrapped;
		++result.secondary_written;
	};

	if (secondary.isNative()) {
		for (const StereoSample sample : work) {
			step();
			secondary.writeNative(sample);
		}
		return result;
	}

	for (const StereoSample sample : work) {
		const int32_t strength2 = secondary.advance(step);
		const int32_t strength1 = 65536 - strength2;
		secondary.writeResampled(sample, strength1, strength2);
	}
	return result;
}

} // namespace deluge::dsp::fixed
