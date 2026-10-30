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
/// @brief The granular effect's workloads: deterministic runs of blocks, each with its knobs and the lifecycle event
/// an owner applies before it, and what a granular effect renders from them.
///
/// A run is generic over the granular class: the reference (reference_granular.h) or the kernel's wrapper, which take
/// the same calls. A block of the same index gets the same input in every run.

#pragma once

#include "definitions_cxx.hpp"
#include "dsp-next/core/stereo_sample.h"
#include "granular_ring.h"
#include "util/synth_noise.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace deluge::dsp::test::granular_workloads {

/// @brief What an owner does to the granular effect before a block.
enum class Event : uint8_t {
	NONE,
	CLEAR, ///< `clearGrainFXBuffer()`: the owner's clear, which ends in acquiring the ring.
	SKIP,  ///< `startSkippingRendering()`: the instrument has gone idle and the ring is released.
};

/// @brief The reverb amount a block passes unless its workload varies it, Q31.
inline constexpr q31_t kSendAmount = 0x10000000;

/// @brief One block of a workload.
struct GranularBlock {
	int32_t rate;                    ///< The rate knob, Q31.
	int32_t mix;                     ///< The mix knob, Q31.
	int32_t density;                 ///< The density knob, Q31.
	int32_t randomness;              ///< The pitch randomness knob, Q31.
	uint16_t n;                      ///< Frames in the block.
	bool sound;                      ///< Whether anything rendered into the block; else its input is silence.
	Event event;                     ///< Applied before the block.
	q31_t send_amount = kSendAmount; ///< The reverb amount, Q31.
	bool extremes = false; ///< The input is INT32_MAX and INT32_MIN alternating every @ref kExtremesRun frames, both
	                       ///< channels alike so that the wet sum is full scale, rather than @ref stereo_input; the
	                       ///< reverb feed starts full scale.
};

/// @brief A named run of blocks and the synth-wide noise generator's state it starts from.
struct GranularWorkload {
	const char* name;
	std::vector<GranularBlock> blocks;
	uint32_t cong_seed;
};

/// @brief The synth-wide generator's state at boot (util/waves.cpp), where every workload starts it.
inline constexpr uint32_t kCongSeed = 380116160;

/// @brief The rate knob: a grain starts about every 14700, 1242 or 245 samples.
inline constexpr int32_t kRateLow = 0x800;
inline constexpr int32_t kRateMid = 0x10000;     ///< @copydoc kRateLow
inline constexpr int32_t kRateHigh = 0x7fffffff; ///< @copydoc kRateLow
inline constexpr int32_t kMixLow = 0x10000000;
inline constexpr int32_t kMixMid = 0x40000000;  ///< @copydoc kMixLow
inline constexpr int32_t kMixHigh = 0x70000000; ///< @copydoc kMixLow
/// @brief The top of the knob, which is dry: the cubic's offset leaves 32 bits and wraps the wet volume to zero.
inline constexpr int32_t kMixMax = INT32_MAX;
inline constexpr int32_t kDensityLow = -0x60000000;
inline constexpr int32_t kDensityMid = 0;           ///< @copydoc kDensityLow
inline constexpr int32_t kDensityHigh = 0x60000000; ///< @copydoc kDensityLow
inline constexpr int32_t kRandomnessNone = 0;
inline constexpr int32_t kRandomnessMid = 0x40000000; ///< @copydoc kRandomnessNone
inline constexpr int32_t kRandomnessMax = INT32_MAX;  ///< @copydoc kRandomnessNone

/// @brief The post-FX volume a run starts each block's chain from, Q31.
inline constexpr int32_t kStartVolume = 0x30000000;

/// @brief The frames an extremes block holds each extreme: a square wave low enough to pass the wet low-pass.
inline constexpr std::size_t kExtremesRun = 32;

/// @brief The block length of the workloads that do not cycle lengths.
inline constexpr uint16_t kBlock = 128;

/// @return @p n frames of a full-range sequence, a linear congruential generator per channel, different for each
/// @p block.
[[nodiscard]] inline std::vector<StereoSample> stereo_input(std::size_t n, std::size_t block) {
	std::vector<StereoSample> v(n);
	uint32_t l = 0x9e3779b9U * static_cast<uint32_t>(block + 1);
	uint32_t r = 0x85ebca6bU * static_cast<uint32_t>(block + 1) + 1;
	for (StereoSample& frame : v) {
		l = (l * 1664525U) + 1013904223U;
		r = (r * 22695477U) + 1U;
		frame = {static_cast<q31_t>(l), static_cast<q31_t>(r)};
	}
	return v;
}

/// @return @p n values of the reverb feed a block starts from: full scale for an extremes block, so the effect's add
/// wraps; else a quarter-scale sequence.
[[nodiscard]] inline std::vector<q31_t> send_input(std::size_t n, std::size_t block, bool extremes) {
	std::vector<q31_t> v(n, INT32_MAX);
	if (!extremes) {
		uint32_t x = 0x7f4a7c15U * static_cast<uint32_t>(block + 1);
		for (q31_t& value : v) {
			x = (x * 1664525U) + 1013904223U;
			value = static_cast<q31_t>(x) >> 2;
		}
	}
	return v;
}

namespace detail {

/// @brief Appends blocks to a workload.
class Builder {
public:
	/// @brief Appends @p count blocks of @p n frames with the knobs set so far, the first carrying @p first_event.
	Builder& run(std::size_t count, uint16_t n = kBlock, Event first_event = Event::NONE, bool sound = true,
	             bool extremes = false) {
		for (std::size_t i = 0; i < count; ++i) {
			blocks_.push_back({.rate = rate_,
			                   .mix = mix_,
			                   .density = density_,
			                   .randomness = randomness_,
			                   .n = n,
			                   .sound = sound,
			                   .event = i == 0 ? first_event : Event::NONE,
			                   .extremes = extremes});
		}
		return *this;
	}

	/// @brief The knobs of the blocks appended from now on.
	Builder& knobs(int32_t rate, int32_t mix, int32_t density, int32_t randomness) {
		rate_ = rate;
		mix_ = mix;
		density_ = density;
		randomness_ = randomness;
		return *this;
	}

	/// @brief Appends one block of @p n frames with @p event.
	Builder& one(uint16_t n, Event event = Event::NONE) { return run(1, n, event); }

	/// @return The frames appended so far.
	[[nodiscard]] std::size_t frames() const {
		std::size_t total = 0;
		for (const GranularBlock& block : blocks_) {
			total += block.n;
		}
		return total;
	}

	[[nodiscard]] std::size_t block_count() const { return blocks_.size(); }

	[[nodiscard]] GranularWorkload build(const char* name) { return {name, std::move(blocks_), kCongSeed}; }

private:
	std::vector<GranularBlock> blocks_;
	int32_t rate_ = kRateHigh;
	int32_t mix_ = kMixMid;
	int32_t density_ = kDensityHigh;
	int32_t randomness_ = kRandomnessMax;
};

} // namespace detail

/// @brief The frames of the first phase of SHORT_BLOCKS: blocks of 1 to 9 frames, past two trips round the ring.
inline constexpr std::size_t kShortPhaseFrames = 140'000;

/// @brief Every workload.
[[nodiscard]] inline const std::vector<GranularWorkload>& all_granular_workloads() {
	static const std::vector<GranularWorkload> all = [] {
		using detail::Builder;
		std::vector<GranularWorkload> w;

		// Past half the ring, where grains start, and past its end.
		w.push_back(Builder{}.run(600).build("FIRST_PASS"));

		{
			Builder b;
			constexpr std::array<int32_t, 3> rates{kRateLow, kRateMid, kRateHigh};
			constexpr std::array<int32_t, 3> mixes{kMixLow, kMixMid, kMixHigh};
			constexpr std::array<int32_t, 3> densities{kDensityLow, kDensityMid, kDensityHigh};
			constexpr std::array<int32_t, 3> randomnesses{kRandomnessNone, kRandomnessMid, kRandomnessMax};
			// Twelve stretches of 25 blocks: each knob in turn at low, mid and high, the others at mid.
			for (std::size_t phase = 0; phase < 12; ++phase) {
				const std::size_t knob = phase / 3;
				const std::size_t level = phase % 3;
				b.knobs(knob == 0 ? rates[level] : kRateMid, knob == 1 ? mixes[level] : kMixMid,
				        knob == 2 ? densities[level] : kDensityMid, knob == 3 ? randomnesses[level] : kRandomnessMid);
				b.run(25, 256);
			}
			GranularWorkload sweep = b.build("KNOB_SWEEP");
			// The reverb amount steps each block through eight values, so the send's amount path sees more than one.
			for (std::size_t i = 0; i < sweep.blocks.size(); ++i) {
				sweep.blocks[i].send_amount = static_cast<q31_t>(((i % 8) + 1) * 0x04000000);
			}
			w.push_back(std::move(sweep));
		}

		// The knobs' extremes, each held long enough for grains to start (after half a ring) and for the cached knob
		// paths to update: the mix at the top (dry), the bottom and zero, then back to wet; the rate at zero and at the
		// bottom, then up; the randomness at the bottom; and the density and rate changing together.
		{
			Builder b;
			b.run(160, 256);
			const auto stretch = [&b](int32_t rate, int32_t mix, int32_t density, int32_t randomness) {
				b.knobs(rate, mix, density, randomness).run(40, 256);
			};
			stretch(kRateHigh, kMixMax, kDensityHigh, kRandomnessMax);
			stretch(kRateHigh, INT32_MIN, kDensityHigh, kRandomnessMax);
			stretch(kRateHigh, 0, kDensityHigh, kRandomnessMax);
			stretch(kRateHigh, kMixMid, kDensityHigh, kRandomnessMax);
			stretch(0, kMixMid, kDensityHigh, kRandomnessMax);
			stretch(INT32_MIN, kMixMid, kDensityHigh, kRandomnessMax);
			stretch(kRateHigh, kMixMid, kDensityHigh, kRandomnessMax);
			stretch(kRateHigh, kMixMid, kDensityHigh, INT32_MIN);
			stretch(kRateHigh, kMixMid, kDensityHigh, kRandomnessMax);
			stretch(kRateMid, kMixMid, kDensityLow, kRandomnessMax);
			w.push_back(b.build("KNOB_EXTREMES"));
		}

		w.push_back(Builder{}.run(600, 512).build("PITCH_CLASSES"));

		// The silence outlasts the longest countdown (four wraps, then one more to go below zero).
		w.push_back(Builder{}.run(300).run(2700, kBlock, Event::NONE, false).run(300).build("DECAY_RESTART"));

		w.push_back(Builder{}.run(200, 256).run(100, 256, Event::NONE, false).build("TAIL"));

		// Each half is longer than half the ring, where grains start.
		w.push_back(Builder{}.run(200, 256).run(1, 256, Event::CLEAR).run(199, 256).build("CLEAR_MID"));

		w.push_back(Builder{}.run(200, 256).run(1, 256, Event::SKIP).run(199, 256).build("SKIP_MID"));

		w.push_back(Builder{}
		                .knobs(kRateHigh, kMixHigh, kDensityHigh, kRandomnessMid)
		                .run(800, kBlock, Event::NONE, true, true)
		                .build("FULL_SCALE"));

		{
			Builder b;
			constexpr std::array<uint16_t, 9> lengths{1, 2, 3, 4, 5, 6, 7, 8, 9};
			for (std::size_t i = 0; b.frames() < kShortPhaseFrames; ++i) {
				b.one(lengths[i % lengths.size()]);
			}
			// A single block to a multiple of the block length, then blocks of 128 to the third lap of the ring: one
			// of those calls starts with the write index at the ring's end.
			const std::size_t to_multiple = (kBlock - (b.frames() % kBlock)) % kBlock;
			if (to_multiple != 0) {
				b.one(static_cast<uint16_t>(to_multiple));
			}
			const std::size_t lap3 = 3 * static_cast<std::size_t>(kModFXGrainBufferSize);
			b.run((lap3 - b.frames()) / kBlock + 20);
			w.push_back(b.build("SHORT_BLOCKS"));
		}
		return w;
	}();
	return all;
}

/// @brief What a run renders: every block's output, the reverb feed, and the post-FX volume after every block.
struct GranularTrace {
	std::vector<int32_t> samples; ///< Each block's frames, left then right, one block after another.
	std::vector<int32_t> inputs;  ///< The same layout, before the effect.
	std::vector<int32_t> sends;   ///< Each block's reverb feed after the call, one block after another.
	std::vector<int32_t> volumes; ///< The volume each block's call leaves; each block starts from @ref kStartVolume.
};

/// @brief Plays @p workload through @p fx: the generator restored to the workload's seed, each block's event applied,
/// then the block rendered.
/// @tparam Fx A granular effect with the reference's calls.
template <typename Fx>
[[nodiscard]] GranularTrace play(const GranularWorkload& workload, Fx& fx) {
	deluge::synth_noise.seed(workload.cong_seed);
	GranularTrace trace;
	for (std::size_t b = 0; b < workload.blocks.size(); ++b) {
		const GranularBlock& block = workload.blocks[b];
		switch (block.event) {
		case Event::NONE:
			break;
		case Event::CLEAR:
			fx.clearGrainFXBuffer();
			break;
		case Event::SKIP:
			fx.startSkippingRendering();
			break;
		}
		std::vector<StereoSample> frames = stereo_input(block.n, b);
		if (block.extremes) {
			for (std::size_t i = 0; i < frames.size(); ++i) {
				const bool even = (i / kExtremesRun) % 2 == 0;
				const q31_t extreme = even ? INT32_MAX : INT32_MIN;
				frames[i] = {extreme, extreme};
			}
		}
		if (!block.sound) {
			std::ranges::fill(frames, StereoSample{0, 0});
		}
		for (const StereoSample frame : frames) {
			trace.inputs.push_back(frame.l);
			trace.inputs.push_back(frame.r);
		}
		std::vector<q31_t> send = send_input(block.n, b, block.extremes);
		int32_t volume = kStartVolume;
		fx.processGrainFX(frames, block.rate, block.mix, block.density, block.randomness, &volume, block.sound,
		                  block.send_amount, send);
		for (const StereoSample frame : frames) {
			trace.samples.push_back(frame.l);
			trace.samples.push_back(frame.r);
		}
		trace.sends.insert(trace.sends.end(), send.begin(), send.end());
		trace.volumes.push_back(volume);
	}
	return trace;
}

} // namespace deluge::dsp::test::granular_workloads
