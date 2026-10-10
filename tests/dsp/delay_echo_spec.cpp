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

// Where the delay's echoes land at the shortest delays, where a 128-frame block laps the tape. The frame-order
// reference (the delay line's order, which the fused kernel takes) and the shipped DelayProcessor put every echo where
// the tape does: D = S - 1 samples on a native tape of S samples, S / spin on a resampling one. The staged order drops
// a native 128-sample tape's echoes once they come round to block frame 0, and damages those at block frame 0 on the
// 128-sample tape resampling just below 1x (.superpowers/findings/delay-lap/).

#include "delay_workloads.h"
#include "reference_delay_processor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using deluge::dsp::StereoSample;
using deluge::dsp::fixed::DelayBlock;
using deluge::dsp::fixed::DelayTape;

namespace {

/// The staged reference: reads the whole block from the tape, then writes it back.
using Staged = delay_reference::ReferenceDelayProcessor;
/// The delay line's order.
using FrameOrder = delay_reference::FrameOrderDelayProcessor;
/// The shipped processor, the delay line's order through the fused kernel.
using deluge::dsp::fixed::DelayProcessor;

/// The echoes of one impulse through the frame-order reference, over a single tape filled with zeros.
struct EchoTrain {
	double delay;                   ///< Where the tape puts each echo, in samples after the last.
	std::vector<double> positions;  ///< Each echo's centroid, in samples after the impulse.
	std::vector<double> amplitudes; ///< Each echo's level, relative to the impulse times feedback^k.
};

/// @return The first @p echoes echoes of an impulse (2^28) at frame @p frame of a 128-frame block, through
/// @p Processor over a tape of @p size samples native at @p native, played at @p rate with digital feedback 0.99,
/// straight. Each echo is found as the largest sample within half a delay of where the tape puts it; its position is
/// the centroid of the seven samples around that peak and its level their sum (a wider sum would take in the feedback
/// high-pass's tail).
template <typename Processor>
EchoTrain echoes(size_t size, uint32_t native, uint32_t rate, int32_t frame, size_t echoes) {
	std::vector<StereoSample> storage(size + static_cast<size_t>(DelayTape::kWriteLag), StereoSample{.l = 0, .r = 0});
	DelayTape tape{std::span{storage}, size, native};
	const double spin = static_cast<double>(rate) / static_cast<double>(native);
	const double delay = rate == native ? static_cast<double>(size) - 1 : static_cast<double>(size) / spin;

	Processor processor;
	processor.resetHighPass();
	const DelayBlock settings{.rate = static_cast<int32_t>(rate),
	                          .feedback = delay_workloads::kFeedback099,
	                          .analog = false,
	                          .analog_saturation = 8,
	                          .ping_pong = false,
	                          .render_in_stereo = true};
	const auto length = static_cast<size_t>((static_cast<double>(echoes) + 1) * delay) + static_cast<size_t>(frame);
	std::vector<double> out;
	std::array<StereoSample, 128> block{};
	for (size_t start = 0; start < length; start += block.size()) {
		block.fill({.l = 0, .r = 0});
		if (start == 0) {
			block.at(static_cast<size_t>(frame)) = delay_workloads::kImpulse;
		}
		std::ignore = processor.process(block, &tape, nullptr, settings);
		for (const StereoSample s : block) {
			out.push_back(static_cast<double>(s.l));
		}
	}

	EchoTrain train{.delay = delay, .positions = {}, .amplitudes = {}};
	double expected = static_cast<double>(delay_workloads::kImpulse.l);
	for (size_t k = 1; k <= echoes; ++k) {
		expected *= static_cast<double>(delay_workloads::kFeedback099) / static_cast<double>(1 << 30);
		const double centre = static_cast<double>(frame) + (static_cast<double>(k) * delay);
		const auto low = static_cast<size_t>(centre - (delay / 2));
		const auto high = std::min(out.size() - 1, static_cast<size_t>(centre + (delay / 2)));
		size_t peak = low;
		for (size_t i = low; i <= high; ++i) {
			peak = std::abs(out[i]) > std::abs(out[peak]) ? i : peak;
		}
		double sum = 0;
		double moment = 0;
		for (size_t i = peak - 3; i <= peak + 3; ++i) {
			sum += out[i];
			moment += out[i] * static_cast<double>(i);
		}
		train.positions.push_back((moment / sum) - static_cast<double>(frame));
		train.amplitudes.push_back(sum / expected);
	}
	return train;
}

/// @return How @p train misses where the tape puts its echoes: "delay d, expected D" when the train's delay (its last
/// echo's position over the echo count) is more than 0.01 samples off; "echo k at p, expected q" for an echo more
/// than half a sample from k times the delay; "echo k at level a" for one carrying less than half the impulse times
/// feedback^k, which is dropped. The feedback high-pass's tail pulls each echo about 0.003 samples early, and the
/// resampled read's interpolation spreads a slowed echo, so a single echo's centroid wanders by up to about 0.15.
std::vector<std::string> misses(const EchoTrain& train) {
	std::vector<std::string> out;
	const double measured = train.positions.back() / static_cast<double>(train.positions.size());
	if (std::abs(measured - train.delay) > 0.01) {
		out.push_back("delay " + std::to_string(measured) + ", expected " + std::to_string(train.delay));
	}
	for (size_t k = 0; k < train.positions.size(); ++k) {
		const double expected = static_cast<double>(k + 1) * train.delay;
		if (std::abs(train.positions[k] - expected) > 0.5) {
			out.push_back("echo " + std::to_string(k + 1) + " at " + std::to_string(train.positions[k]) + ", expected "
			              + std::to_string(expected));
		}
		if (train.amplitudes[k] < 0.5) {
			out.push_back("echo " + std::to_string(k + 1) + " at level " + std::to_string(train.amplitudes[k]));
		}
	}
	return out;
}

/// @return The echoes of @p train, counted from 1, carrying less than half the impulse times feedback^k.
std::vector<size_t> dropped(const EchoTrain& train) {
	std::vector<size_t> out;
	for (size_t k = 0; k < train.amplitudes.size(); ++k) {
		if (train.amplitudes[k] < 0.5) {
			out.push_back(k + 1);
		}
	}
	return out;
}

/// A tape the echo spec plays an impulse through: its size, native rate and the rate it plays at.
struct EchoTape {
	std::string_view name;
	size_t size;
	uint32_t native;
	uint32_t rate;
};

/// The shortest delays the owner makes, where the staged order laps on 128-frame blocks, and their neighbours.
const std::array<EchoTape, 4> kEchoTapes{{
    {.name = "native 128", .size = 128, .native = 2147483647u, .rate = 2147483647u},
    {.name = "128 at 2^31 native", .size = 128, .native = 2147483648u, .rate = 2147483647u},
    {.name = "129 at 0.999x", .size = 129, .native = 2130836488u, .rate = 2128705651u},
    {.name = "129 at 1.001x", .size = 129, .native = 2130836488u, .rate = 2132967324u},
}};

} // namespace

// clang-format off
describe delay_echo("Delay echoes", ${
	it("puts every echo where the tape does at the shortest delays, D = S - 1 native and S / spin resampling, and "
	   "drops none over eight at feedback 0.99, wherever in the block the impulse falls", _ {
		std::vector<std::string> missed;
		for (const EchoTape& tape : kEchoTapes) {
			for (const int32_t frame : {0, 5, 127}) {
				for (const std::string& miss :
				     misses(echoes<FrameOrder>(tape.size, tape.native, tape.rate, frame, 8))) {
					missed.push_back(std::string{tape.name} + " frame " + std::to_string(frame) + ": " + miss);
				}
			}
		}
		expect(missed).to_equal(std::vector<std::string>{});
	});

	it("plays every echo through the shipped processor where the tape puts it at the shortest delays, wherever in the "
	   "block the impulse falls", _ {
		std::vector<std::string> missed;
		for (const EchoTape& tape : kEchoTapes) {
			for (const int32_t frame : {0, 5, 127}) {
				for (const std::string& miss :
				     misses(echoes<DelayProcessor>(tape.size, tape.native, tape.rate, frame, 8))) {
					missed.push_back(std::string{tape.name} + " frame " + std::to_string(frame) + ": " + miss);
				}
			}
		}
		expect(missed).to_equal(std::vector<std::string>{});
	});

	it("measures the staged order dropping the native 128-sample tape's echoes: an impulse at block frame f keeps f",
	   _ {
		const EchoTape& tape = kEchoTapes.front();
		expect(dropped(echoes<Staged>(tape.size, tape.native, tape.rate, 0, 8)))
		    .to_equal(std::vector<size_t>{1, 2, 3, 4, 5, 6, 7, 8});
		expect(dropped(echoes<Staged>(tape.size, tape.native, tape.rate, 5, 8))).to_equal(std::vector<size_t>{6, 7, 8});
		expect(dropped(echoes<FrameOrder>(tape.size, tape.native, tape.rate, 5, 8))).to_equal(std::vector<size_t>{});
	});

	it("measures the staged order damaging the 128-sample tape's echoes at block frame 0 while it resamples just "
	   "below 1x", _ {
		// The frame-0 write is four samples wide, and frame 127 reads it before the staged write lands: each pass loses
		// part of the echo and pulls the rest late, 0.2 samples over eight echoes, the fourth on below half its level.
		const EchoTape& tape = kEchoTapes.at(1);
		const EchoTrain staged = echoes<Staged>(tape.size, tape.native, tape.rate, 0, 8);
		expect(dropped(staged)).to_equal(std::vector<size_t>{4, 5, 6, 7, 8});
		expect(staged.positions.back() / 8).to_be_greater_than(staged.delay + 0.15);
		expect(misses(echoes<FrameOrder>(tape.size, tape.native, tape.rate, 0, 8))).to_equal(std::vector<std::string>{});
	});
});
// clang-format on

CPPSPEC_MAIN(delay_echo);
