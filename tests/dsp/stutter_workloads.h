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

/// @file
/// @brief The stutter's workloads: a stutter begun at a rate with its settings, then blocks of a length, a rate and an
/// input each, and what a stutter renders from them.
///
/// A run is generic over the stutter: the reference (reference_stutter.h) or the kernel's component, which take the
/// same calls. A block of the same index gets the same input in every run.

#pragma once

#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/delay/delay_tape.h"
#include "engines/effects/state.h"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ostream>
#include <span>
#include <vector>

namespace stutter_workloads {

using deluge::dsp::StereoSample;
using deluge::dsp::fixed::DelayTape;
using deluge::effects::StutterState;

/// @brief Every workload, in all_workloads() order.
enum class WorkloadId : uint8_t {
	RECORD_NATIVE,
	RECORD_RESAMPLED,
	PLAY_FORWARD_NATIVE,
	PLAY_FORWARD_RESAMPLED,
	PLAY_REVERSED_NATIVE,
	PLAY_REVERSED_RESAMPLED,
	PING_PONG_NATIVE,
	PING_PONG_RESAMPLED,
	RATE_MOVES,
	BLOCKS_1_TO_9,
	FULL_SCALE,
	CLAMPED,
	kCount,
};
inline constexpr std::size_t kWorkloadCount = static_cast<std::size_t>(WorkloadId::kCount);

/// @brief The neutral rate (16777216): a tape of DelayTape::kNeutralSize frames.
inline constexpr uint32_t kNeutral = 1U << 24;
/// @brief The rate most workloads begin at: a tape of 2048 frames, recorded in 16 blocks of 128.
inline constexpr uint32_t kRate = 8 * kNeutral;
/// @brief The largest block a workload renders.
inline constexpr std::size_t kMaxFrames = 128;

/// @brief One block: its length, the rate it renders at, and whether its input is full scale.
struct Step {
	uint16_t frames;
	uint32_t rate;
	bool full_scale = false;
};

/// @brief A stutter begun at @ref begin_rate with @ref settings, then @ref blocks blocks as @ref step gives them.
struct Workload {
	WorkloadId id;
	const char* name;
	uint32_t begin_rate;
	StutterState settings;
	std::size_t blocks;
	std::function<Step(std::size_t)> step;
};

/// @return @p n frames of block @p block's input: a full-range sequence per channel, different for each block; or,
/// for a full-scale block, runs of 512 frames of INT32_MAX then INT32_MIN on both channels, so the resampled play's
/// two taps sum to the edge of full scale.
[[nodiscard]] inline std::vector<StereoSample> input(std::size_t n, std::size_t block, bool full_scale,
                                                     std::size_t first_frame) {
	std::vector<StereoSample> v(n);
	if (full_scale) {
		for (std::size_t i = 0; i < n; ++i) {
			const q31_t extreme = ((first_frame + i) / 512) % 2 == 0 ? INT32_MAX : INT32_MIN;
			v[i] = {extreme, extreme};
		}
		return v;
	}
	uint32_t l = 0x9e3779b9U * static_cast<uint32_t>(block + 1);
	uint32_t r = 0x85ebca6bU * static_cast<uint32_t>(block + 1) + 1;
	for (StereoSample& frame : v) {
		l = (l * 1664525U) + 1013904223U;
		r = (r * 22695477U) + 1U;
		frame = {static_cast<q31_t>(l), static_cast<q31_t>(r)};
	}
	return v;
}

/// @brief Every workload.
[[nodiscard]] inline const std::vector<Workload>& all_workloads() {
	static const std::vector<Workload> all = [] {
		std::vector<Workload> w;
		const auto steady = [](uint32_t rate) {
			return [rate](std::size_t) { return Step{.frames = 128, .rate = rate}; };
		};
		// Moved off the begin rate after @p from blocks: slower (a slowed-down write or read) or faster (sped up).
		const auto moved = [](std::size_t from, uint32_t to) {
			return [from, to](std::size_t b) { return Step{.frames = 128, .rate = b < from ? kRate : to}; };
		};
		const StutterState forward{.quantized = false};
		const StutterState reversed{.quantized = false, .reversed = true};
		const StutterState ping_pong{.quantized = false, .ping_pong = true};

		// The tape recorded and a few blocks played: native, and resampled from block 4 on, the rate 3/4 of the begin
		// rate (a slowed-down write) then, in PLAY_FORWARD_RESAMPLED, 5/4 (a sped-up read).
		w.push_back({WorkloadId::RECORD_NATIVE, "RECORD_NATIVE", kRate, forward, 20, steady(kRate)});
		w.push_back({WorkloadId::RECORD_RESAMPLED, "RECORD_RESAMPLED", kRate, forward, 30, moved(4, kRate / 4 * 3)});
		// Recorded at the begin rate, then three laps and more of the tape played.
		w.push_back({WorkloadId::PLAY_FORWARD_NATIVE, "PLAY_FORWARD_NATIVE", kRate, forward, 70, steady(kRate)});
		w.push_back({WorkloadId::PLAY_FORWARD_RESAMPLED, "PLAY_FORWARD_RESAMPLED", kRate, forward, 70,
		             moved(20, kRate / 4 * 5)});
		w.push_back({WorkloadId::PLAY_REVERSED_NATIVE, "PLAY_REVERSED_NATIVE", kRate, reversed, 70, steady(kRate)});
		w.push_back({WorkloadId::PLAY_REVERSED_RESAMPLED, "PLAY_REVERSED_RESAMPLED", kRate, reversed, 70,
		             moved(20, kRate / 4 * 3)});
		w.push_back({WorkloadId::PING_PONG_NATIVE, "PING_PONG_NATIVE", kRate, ping_pong, 70, steady(kRate)});
		w.push_back(
		    {WorkloadId::PING_PONG_RESAMPLED, "PING_PONG_RESAMPLED", kRate, ping_pong, 70, moved(20, kRate / 4 * 5)});
		// Recorded natively, then the rate stepped every 8 blocks while it plays: faster, slower, far faster, back to
		// the begin rate (which a resampling tape keeps resampling at), and slower still.
		w.push_back({WorkloadId::RATE_MOVES, "RATE_MOVES", kRate, ping_pong, 70, [](std::size_t b) {
			             constexpr std::array<uint32_t, 6> rates{kRate,     kRate / 2 * 3, kRate / 2,
			                                                     kRate * 5, kRate,         kRate / 3};
			             return Step{.frames = 128, .rate = b < 20 ? kRate : rates[((b - 20) / 8) % rates.size()]};
		             }});
		// Blocks of 1 to 9 frames, ping-pong so the turns land mid-block, resampled from block 1200 on (past the
		// recording's ~410 blocks and a lap played).
		w.push_back({WorkloadId::BLOCKS_1_TO_9, "BLOCKS_1_TO_9", kRate, ping_pong, 2400, [](std::size_t b) {
			             return Step{.frames = static_cast<uint16_t>((b % 9) + 1),
			                         .rate = b < 1200 ? kRate : kRate / 4 * 5};
		             }});
		// Full-scale input, played resampled so the two taps sum to the edge.
		w.push_back({WorkloadId::FULL_SCALE, "FULL_SCALE", kRate, ping_pong, 70, [](std::size_t b) {
			             return Step{.frames = 128, .rate = b < 20 ? kRate : kRate / 4 * 3, .full_scale = true};
		             }});
		// A rate slow enough that the tape is clamped to its largest size, its native rate made precise, so it
		// resamples from the first block, at about 2/3 of a step a frame: recorded in about 1030 blocks.
		w.push_back({WorkloadId::CLAMPED, "CLAMPED", kNeutral / 8, forward, 1140, steady(kNeutral / 8)});
		return w;
	}();
	return all;
}

/// @brief A 64-bit FNV-1a digest of 32-bit words.
struct Digest {
	uint64_t value = 14695981039346656037ULL;

	void add(uint32_t word) { value = (value ^ word) * 1099511628211ULL; }
	void add(int32_t word) { add(static_cast<uint32_t>(word)); }
	void add(std::span<const StereoSample> samples) {
		for (const StereoSample s : samples) {
			add(s.l);
			add(s.r);
		}
	}
};

/// @brief What a workload rendered: a digest of every block's output, of the lifecycle after every block (playing,
/// direction, the frames left to record, and the tape's head), and of the tape's storage at the end.
struct WorkloadRecord {
	uint64_t outputs = 0;
	uint64_t lifecycle = 0;
	uint64_t tape = 0;

	bool operator==(const WorkloadRecord&) const = default;
};

inline std::ostream& operator<<(std::ostream& out, const WorkloadRecord& r) {
	return out << std::hex << "{.outputs = 0x" << r.outputs << "ULL, .lifecycle = 0x" << r.lifecycle
	           << "ULL, .tape = 0x" << r.tape << "ULL}" << std::dec;
}

/// @brief Folds @p tape's head into @p digest.
inline void add_head(Digest& digest, const DelayTape& tape) {
	const DelayTape::Head head = tape.head();
	digest.add(static_cast<uint32_t>(head.pos - tape.begin()));
	digest.add(head.long_pos);
	digest.add(static_cast<uint32_t>(head.last_short_pos));
	digest.add(static_cast<uint32_t>(tape.resampling()));
}

/// @brief Plays @p workload through @p stutter, calling @p each with every block's index and output.
/// @tparam Stutter A stutter with the reference's calls.
template <typename Stutter, typename Each>
void play(const Workload& workload, Stutter& stutter, Each&& each, StereoSample fill = {.l = 0, .r = 0}) {
	stutter.begin(workload.begin_rate, workload.settings, fill);
	std::size_t frame = 0;
	for (std::size_t b = 0; b < workload.blocks; ++b) {
		const Step step = workload.step(b);
		std::vector<StereoSample> block = input(step.frames, b, step.full_scale, frame);
		frame += step.frames;
		stutter.process(block, step.rate);
		each(b, std::span<const StereoSample>{block});
	}
}

/// @return @p workload's record, rendered by a fresh @p Stutter whose storage starts as @p fill.
template <typename Stutter>
[[nodiscard]] WorkloadRecord record(const Workload& workload, StereoSample fill = {.l = 0, .r = 0}) {
	Stutter stutter;
	WorkloadRecord out;
	Digest outputs;
	Digest lifecycle;
	play(
	    workload, stutter,
	    [&](std::size_t, std::span<const StereoSample> block) {
		    outputs.add(block);
		    lifecycle.add(static_cast<uint32_t>(stutter.playing()));
		    lifecycle.add(static_cast<uint32_t>(stutter.reversed()));
		    lifecycle.add(stutter.frames_left());
		    add_head(lifecycle, stutter.tape());
	    },
	    fill);
	out.outputs = outputs.value;
	out.lifecycle = lifecycle.value;
	Digest tape;
	tape.add(std::span<const StereoSample>{stutter.tape().begin(), stutter.tape().end()});
	out.tape = tape.value;
	return out;
}

} // namespace stutter_workloads
