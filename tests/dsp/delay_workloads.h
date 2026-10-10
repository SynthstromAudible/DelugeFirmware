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
/// @brief The delay's recorded workloads: deterministic runs of blocks over real tapes, driven by a line-for-line
/// mirror of the lifecycle `Delay::process` keeps around its processor (src/deluge/dsp/delay/delay.cpp), and what
/// today's `DelayProcessor` renders from each.
///
/// A workload gives, per block, the frame count, the rate (as a uint32_t, the way the tape takes it) and the block's
/// settings, and starts from a tape setup: a delay switched on (its first tape a secondary, read as silence until it
/// is swapped in) or a primary tape already playing, holding a lap of noise. Fresh allocations hold a known fill, so
/// any sample a tape writes or reads before clearing is pinned too. The abandon count is not mirrored: an abandoned
/// delay stops rendering, and no workload runs long enough to abandon.

#pragma once

#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/delay/delay_processor.h"
#include "dsp-next/fixed/delay/delay_tape.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace delay_workloads {

using deluge::dsp::StereoSample;
using deluge::dsp::fixed::DelayBlock;
using deluge::dsp::fixed::DelayBlockResult;
using deluge::dsp::fixed::DelayTape;

/// @brief Every recorded workload.
enum class WorkloadId : uint8_t {
	NATIVE_STRAIGHT_DIGITAL, ///< The mode combinations: tape form × feedback crossing × shaper, 40 blocks each.
	NATIVE_STRAIGHT_ANALOG,
	NATIVE_CROSS_DIGITAL,
	NATIVE_CROSS_ANALOG,
	RES_LO_STRAIGHT_DIGITAL,
	RES_LO_STRAIGHT_ANALOG,
	RES_LO_CROSS_DIGITAL,
	RES_LO_CROSS_ANALOG,
	RES_HI_STRAIGHT_DIGITAL,
	RES_HI_STRAIGHT_ANALOG,
	RES_HI_CROSS_DIGITAL,
	RES_HI_CROSS_ANALOG,
	MONO_PING_PONG,         ///< Ping-pong in a mono render: mixes straight.
	RATE_SWEEP,             ///< The rate swept across the native rate, up and back down.
	GROWTH_SWAP,            ///< Secondaries grown through their swaps: native, then resampling above its rate.
	GROWTH_MOVING,          ///< A secondary grown while the rate keeps moving below its native rate.
	READ_WRAPS,             ///< A small tape whose read head wraps every block or two, at odd block lengths.
	FIRST_BLOCKS,           ///< A new delay's first blocks: no primary, the secondary filling, then swapped in.
	FIRST_BLOCKS_RESAMPLED, ///< Likewise, the rate moving while the first tape fills.
	HIGHEST_RATE,           ///< The neutral tape stepped to the highest rate the owner sets (INT32_MAX): 128×.
	SPIN_689,               ///< A 2 s tape stepped to the highest rate: 689×, which wraps to 177 steps per sample.
	SPIN_689_SECONDARY,     ///< A 2 s secondary grown while the rate steps to the highest: 177 steps per sample.
	SPIN_255,               ///< A 2 s tape at exactly 255 steps per sample.
	SPIN_FLOOR,             ///< The 128-sample tape at the lowest rate: the 2^-15 crawl, then rate 0.
	NATIVE_RATE_RESAMPLING, ///< A resampling tape played on at exactly its native rate.
	RATE_ABOVE_2_31,        ///< A rate above 2^31, as a uint32_t: reached only through the synced rate's overflow.
};

/// @brief How many workloads there are.
inline constexpr size_t kWorkloadCount = static_cast<size_t>(WorkloadId::RATE_ABOVE_2_31) + 1;

/// @brief The tapes a workload starts from.
struct TapeSetup {
	enum class Start : uint8_t {
		ACTIVATE, ///< The delay switched on at `rate` (Delay::informWhetherActive): a secondary, sized for it.
		PRIMARY,  ///< A primary tape sized for `rate`, as DelayBuffer::init sizes it, holding a lap of noise.
	};
	Start start;
	uint32_t rate;
};

/// @brief One block of a workload.
struct BlockStep {
	size_t frames;       ///< The block's length, in [1, 128].
	uint32_t rate;       ///< The rate, as the tape takes it (DelayBlock::rate is its int32_t image).
	DelayBlock settings; ///< The block's settings, its rate the rate's.
	bool input;          ///< Whether the block carries input; else it is silence.
};

/// @brief A recorded workload: its start and, per block, its step.
struct Workload {
	WorkloadId id;
	std::string_view name;
	TapeSetup tapes;
	size_t blocks;
	std::function<BlockStep(size_t)> step;
};

/// @brief The longest block a workload renders.
inline constexpr size_t kMaxFrames = 128;

namespace detail {

/// The rate whose tape is 2048 samples, native.
inline constexpr uint32_t kRate2048 = 1u << 27;
/// The neutral rate: a 16384-sample tape.
inline constexpr uint32_t kNeutralRate = 16777216;
/// The highest rate the owner sets (a host chain's exp_curve, saturated).
inline constexpr uint32_t kHighestRate = 2147483647;
/// The lowest rate the owner sets: its tape is clamped to 2 s and made native at 3116530.
inline constexpr uint32_t kLowestRate = 65536;
/// The 2 s tape's native rate, made precise.
inline constexpr uint32_t kTwoSecondNative = 3116530;

/// The settings that are not the rate.
struct Mode {
	bool analog = false;
	int32_t saturation = 8;
	bool ping_pong = false;
	bool stereo = true;
	int32_t feedback = 0x38000000;
};

[[nodiscard]] inline BlockStep step(size_t frames, uint32_t rate, const Mode& mode, bool input) {
	return {.frames = frames,
	        .rate = rate,
	        .settings = {.rate = static_cast<int32_t>(rate),
	                     .feedback = mode.feedback,
	                     .analog = mode.analog,
	                     .analog_saturation = mode.saturation,
	                     .ping_pong = mode.ping_pong,
	                     .render_in_stereo = mode.stereo},
	        .input = input};
}

/// One mode combination: a 2048-sample tape at its native rate, three quarters of it or one and a half times it.
[[nodiscard]] inline Workload combination(WorkloadId id, std::string_view name, uint32_t rate, bool cross,
                                          bool analog) {
	const Mode mode{.analog = analog, .saturation = cross ? 5 : 8, .ping_pong = cross, .stereo = true};
	return {.id = id,
	        .name = name,
	        .tapes = {.start = TapeSetup::Start::PRIMARY, .rate = kRate2048},
	        .blocks = 40,
	        .step = [=](size_t b) { return step(kMaxFrames, rate, mode, b < 30); }};
}

} // namespace detail

/// @return Workload @p id.
[[nodiscard]] inline Workload delay_workload(WorkloadId id) {
	using namespace detail;
	using Start = TapeSetup::Start;
	constexpr uint32_t kNative = kRate2048;
	constexpr uint32_t kLo = kRate2048 / 4 * 3;
	constexpr uint32_t kHi = kRate2048 / 2 * 3;
	switch (id) {
	case WorkloadId::NATIVE_STRAIGHT_DIGITAL:
		return combination(id, "native_straight_digital", kNative, false, false);
	case WorkloadId::NATIVE_STRAIGHT_ANALOG:
		return combination(id, "native_straight_analog", kNative, false, true);
	case WorkloadId::NATIVE_CROSS_DIGITAL:
		return combination(id, "native_cross_digital", kNative, true, false);
	case WorkloadId::NATIVE_CROSS_ANALOG:
		return combination(id, "native_cross_analog", kNative, true, true);
	case WorkloadId::RES_LO_STRAIGHT_DIGITAL:
		return combination(id, "res_lo_straight_digital", kLo, false, false);
	case WorkloadId::RES_LO_STRAIGHT_ANALOG:
		return combination(id, "res_lo_straight_analog", kLo, false, true);
	case WorkloadId::RES_LO_CROSS_DIGITAL:
		return combination(id, "res_lo_cross_digital", kLo, true, false);
	case WorkloadId::RES_LO_CROSS_ANALOG:
		return combination(id, "res_lo_cross_analog", kLo, true, true);
	case WorkloadId::RES_HI_STRAIGHT_DIGITAL:
		return combination(id, "res_hi_straight_digital", kHi, false, false);
	case WorkloadId::RES_HI_STRAIGHT_ANALOG:
		return combination(id, "res_hi_straight_analog", kHi, false, true);
	case WorkloadId::RES_HI_CROSS_DIGITAL:
		return combination(id, "res_hi_cross_digital", kHi, true, false);
	case WorkloadId::RES_HI_CROSS_ANALOG:
		return combination(id, "res_hi_cross_analog", kHi, true, true);

	case WorkloadId::MONO_PING_PONG:
		// Ping-pong with the engine rendering in mono, resampling above the native rate.
		return {.id = id,
		        .name = "mono_ping_pong",
		        .tapes = {.start = Start::PRIMARY, .rate = kNative},
		        .blocks = 24,
		        .step = [](size_t b) {
			        return step(kMaxFrames, kHi, {.analog = true, .ping_pong = true, .stereo = false}, b < 16);
		        }};

	case WorkloadId::RATE_SWEEP:
		// 0.6x up to 1.6x in steps of 0.05x, through exactly 1x at block 8, then back down through 1x at block 32.
		return {.id = id,
		        .name = "rate_sweep",
		        .tapes = {.start = Start::PRIMARY, .rate = kNative},
		        .blocks = 41,
		        .step = [](size_t b) {
			        const auto twentieths = static_cast<uint64_t>(b <= 20 ? 12 + b : 52 - b);
			        const auto rate = static_cast<uint32_t>(uint64_t{kNative} * twentieths / 20);
			        return step(kMaxFrames, rate, {.ping_pong = true, .stereo = true}, b < 36);
		        }};

	case WorkloadId::GROWTH_SWAP:
		// A 1024-sample delay switched on (its secondary swapped in after block 8); the rate tripled at block 12 (the
		// primary steps three times per sample while a 341-sample secondary grows just below its precise native rate,
		// then swaps in); then a ninth of it at block 24 (a 6144-sample secondary native at a sixth of the rate, so its
		// head steps twice per sample until it swaps in).
		return {.id = id,
		        .name = "growth_swap",
		        .tapes = {.start = Start::ACTIVATE, .rate = 1u << 28},
		        .blocks = 56,
		        .step = [](size_t b) {
			        const uint32_t rate = b < 12 ? 1u << 28 : b < 24 ? 3u << 28 : (3u << 28) / 9;
			        return step(kMaxFrames, rate, {.analog = (b / 8) % 2 == 1, .saturation = 5}, b < 48);
		        }};

	case WorkloadId::GROWTH_MOVING:
		// A 512-sample delay switched on; at block 10 the rate falls to 0.4x (a 2560-sample secondary at 0.2x), then
		// moves between 0.18x and 0.11x, so the secondary grows below its native rate.
		return {.id = id,
		        .name = "growth_moving",
		        .tapes = {.start = Start::ACTIVATE, .rate = 1u << 29},
		        .blocks = 48,
		        .step = [](size_t b) {
			        constexpr uint32_t kBase = 1u << 29;
			        const uint32_t rate = b < 10    ? kBase
			                              : b == 10 ? kBase / 10 * 4
			                                        : kBase / 100 * (18 - static_cast<uint32_t>(b % 8));
			        return step(kMaxFrames, rate, {.ping_pong = true, .stereo = true}, b < 40);
		        }};

	case WorkloadId::READ_WRAPS:
		// A 200-sample tape, at odd block lengths: native, then 0.9x, then 1.5x (192 steps in a 128-sample block).
		return {.id = id,
		        .name = "read_wraps",
		        .tapes = {.start = Start::PRIMARY, .rate = 1374389534},
		        .blocks = 42,
		        .step = [](size_t b) {
			        constexpr std::array<size_t, 7> kFrames{128, 1, 77, 128, 5, 127, 64};
			        constexpr uint32_t kRate = 1374389534;
			        const uint32_t rate = b < 14 ? kRate : b < 28 ? kRate / 10 * 9 : kRate / 2 * 3;
			        return step(kFrames[b % kFrames.size()], rate, {.analog = b % 3 == 0, .saturation = 5}, b < 36);
		        }};

	case WorkloadId::FIRST_BLOCKS:
		// A 2048-sample delay switched on: 16 blocks with no primary, the secondary filling, then the swap.
		return {.id = id,
		        .name = "first_blocks",
		        .tapes = {.start = Start::ACTIVATE, .rate = kNative},
		        .blocks = 26,
		        .step = [](size_t /*b*/) {
			        return step(kMaxFrames, kNative, {.analog = true, .saturation = 5, .ping_pong = true}, true);
		        }};

	case WorkloadId::FIRST_BLOCKS_RESAMPLED:
		// A 1024-sample delay switched on, the rate at 1.3x for two blocks while the first tape fills, then 0.7x.
		return {.id = id,
		        .name = "first_blocks_resampled",
		        .tapes = {.start = Start::ACTIVATE, .rate = 1u << 28},
		        .blocks = 20,
		        .step = [](size_t b) {
			        constexpr uint32_t kBase = 1u << 28;
			        const uint32_t rate = b < 2 ? kBase : b < 4 ? kBase / 10 * 13 : kBase / 10 * 7;
			        return step(kMaxFrames, rate, {}, true);
		        }};

	case WorkloadId::HIGHEST_RATE:
		// The neutral tape playing, then the highest rate from block 2: the primary steps 128 times per sample until
		// a 128-sample secondary swaps in.
		return {.id = id,
		        .name = "highest_rate",
		        .tapes = {.start = Start::PRIMARY, .rate = kNeutralRate},
		        .blocks = 12,
		        .step = [](size_t b) { return step(kMaxFrames, b < 2 ? kNeutralRate : kHighestRate, {}, true); }};

	case WorkloadId::SPIN_689:
		// The lowest rate's 2 s tape, then the highest rate from block 2: 689x, which the 8.24 spin wraps to 177.06x,
		// 177 or 178 steps per sample, until a 128-sample secondary swaps in.
		return {.id = id,
		        .name = "spin_689",
		        .tapes = {.start = Start::PRIMARY, .rate = kLowestRate},
		        .blocks = 10,
		        .step = [](size_t b) { return step(kMaxFrames, b < 2 ? kLowestRate : kHighestRate, {}, true); }};

	case WorkloadId::SPIN_689_SECONDARY:
		// A 44099-sample tape; the lowest rate at block 1 grows a 2 s secondary (native at 3116530), and the highest
		// from block 2 spins it at 689x, 177 or 178 steps per sample, while the primary's 344x wraps to 88 or 89.
		return {.id = id,
		        .name = "spin_689_secondary",
		        .tapes = {.start = Start::PRIMARY, .rate = 2 * kTwoSecondNative},
		        .blocks = 12,
		        .step = [](size_t b) {
			        const uint32_t rate = b == 0 ? 2 * kTwoSecondNative : b == 1 ? kLowestRate : kHighestRate;
			        return step(kMaxFrames, rate, {.analog = true, .saturation = 5}, true);
		        }};

	case WorkloadId::SPIN_255:
		// The 2 s tape at 255 times its native rate: exactly 255 steps per sample.
		return {.id = id,
		        .name = "spin_255",
		        .tapes = {.start = Start::PRIMARY, .rate = kLowestRate},
		        .blocks = 8,
		        .step = [](size_t /*b*/) { return step(kMaxFrames, 255 * kTwoSecondNative, {}, true); }};

	case WorkloadId::SPIN_FLOOR:
		// The highest rate's 128-sample tape at the lowest rate: 2^-15 of its native rate (made 2^31), the slowest a
		// tape resamples; then rate 0, which crawls at the same spin.
		return {.id = id,
		        .name = "spin_floor",
		        .tapes = {.start = Start::PRIMARY, .rate = kHighestRate},
		        .blocks = 40,
		        .step = [](size_t b) { return step(kMaxFrames, b < 30 ? kLowestRate : 0, {}, b < 20); }};

	case WorkloadId::NATIVE_RATE_RESAMPLING:
		// 1.5x for four blocks starts the tape resampling; it stays resampling back at exactly its native rate.
		return {.id = id,
		        .name = "native_rate_resampling",
		        .tapes = {.start = Start::PRIMARY, .rate = kNative},
		        .blocks = 30,
		        .step = [](size_t b) {
			        return step(kMaxFrames, b < 4 ? kHi : kNative, {.analog = b % 2 == 0, .saturation = 5}, b < 24);
		        }};

	case WorkloadId::RATE_ABOVE_2_31:
		// The neutral tape playing, then a rate of 0x90000000 from block 2: the tape reads it as a negative int32_t,
		// and a 113-sample secondary grows, wrapping many times in the block, and swaps in at once.
		return {.id = id,
		        .name = "rate_above_2_31",
		        .tapes = {.start = Start::PRIMARY, .rate = kNeutralRate},
		        .blocks = 12,
		        .step = [](size_t b) { return step(kMaxFrames, b < 2 ? kNeutralRate : 0x9000'0000u, {}, true); }};
	}
	return {};
}

/// @return Every workload, in WorkloadId order.
[[nodiscard]] inline std::vector<Workload> all_workloads() {
	std::vector<Workload> out;
	for (size_t i = 0; i < kWorkloadCount; ++i) {
		out.push_back(delay_workload(static_cast<WorkloadId>(i)));
	}
	return out;
}

/// @return @p tape's storage as bytes, in place; empty when it has none.
[[nodiscard]] inline std::span<const std::byte> tape_storage(const DelayTape& tape) {
	if (!tape.isActive()) {
		return {};
	}
	return std::as_bytes(std::span<const StereoSample>{tape.begin(), tape.end()});
}

/// @return @p tape's storage, byte for byte; empty when it has none.
[[nodiscard]] inline std::vector<uint8_t> tape_bytes(const DelayTape& tape) {
	const std::span<const std::byte> bytes = tape_storage(tape);
	std::vector<uint8_t> out(bytes.size());
	if (!bytes.empty()) {
		std::memcpy(out.data(), bytes.data(), bytes.size());
	}
	return out;
}

/// @brief A 64-bit FNV-1a over 32-bit words.
struct Digest {
	uint64_t value = 14695981039346656037ull;

	void add(uint32_t word) { value = (value ^ word) * 1099511628211ull; }
	void add(int32_t word) { add(static_cast<uint32_t>(word)); }
	void add(std::span<const StereoSample> samples) {
		for (const StereoSample s : samples) {
			add(s.l);
			add(s.r);
		}
	}
};

/// @return The fill a fresh allocation holds: word @p word of the storage.
[[nodiscard]] inline int32_t allocator_fill(size_t word) {
	return static_cast<int32_t>(0x5A5A0000u + static_cast<uint32_t>(word));
}

/// @return Sample @p k of a workload's input: two unrelated pseudo-random channels, 28 bits of magnitude.
[[nodiscard]] inline StereoSample input(uint32_t k) {
	const uint32_t a = (k + 1) * 2654435761u;
	const uint32_t b = (k + 7) * 2246822519u;
	return {.l = static_cast<int32_t>(a) >> 3, .r = static_cast<int32_t>(b) >> 3};
}

/// @return Sample @p k of a lap of noise on a tape already playing, 26 bits of magnitude.
[[nodiscard]] inline StereoSample tape_noise(uint32_t k) {
	const uint32_t a = (k + 3) * 2246822519u;
	const uint32_t b = (k + 5) * 3266489917u;
	return {.l = static_cast<int32_t>(a) >> 5, .r = static_cast<int32_t>(b) >> 5};
}

/// @brief A tape and the storage it plays, as DelayBuffer allocates it: the ideal size for its rate plus kWriteLag.
struct Buffer {
	std::vector<StereoSample> storage;
	DelayTape tape;

	/// @brief Sizes the storage for @p rate, as DelayBuffer::init does, filled by @p fill(i) for sample i.
	/// @return Whether a buffer was made: not when the ideal size is @p fail_if_this_size.
	bool init(uint32_t rate, uint32_t fail_if_this_size, const std::function<StereoSample(size_t)>& fill) {
		const auto [size, clamped] = DelayTape::idealSizeForRate(rate);
		if (static_cast<uint32_t>(size) == fail_if_this_size) {
			return false;
		}
		const size_t samples = static_cast<size_t>(size) + static_cast<size_t>(DelayTape::kWriteLag);
		storage.resize(samples);
		for (size_t i = 0; i < samples; ++i) {
			storage[i] = fill(i);
		}
		tape = DelayTape{std::span{storage}, static_cast<size_t>(size), rate};
		if (clamped) {
			tape.makeNativeRatePrecise();
		}
		return true;
	}

	/// @brief As a fresh allocation: the allocator's fill.
	bool allocate(uint32_t rate, uint32_t fail_if_this_size) {
		return init(rate, fail_if_this_size, [](size_t i) {
			return StereoSample{.l = allocator_fill(2 * i), .r = allocator_fill((2 * i) + 1)};
		});
	}
};

/// @brief A workload played through @p Processor (anything with DelayProcessor's `process` and `resetHighPass`),
/// with the lifecycle Delay::process keeps around it, mirrored line for line: the rate's settling, a secondary tape
/// grown when the rate leaves the primary's range, and the swap once it is full.
template <typename Processor>
class WorkloadRun {
public:
	explicit WorkloadRun(Workload workload) : workload_{std::move(workload)} {
		static_assert(std::is_default_constructible_v<Processor>);
		if (workload_.tapes.start == TapeSetup::Start::ACTIVATE) {
			// Delay::informWhetherActive, then prepareToBeginWriting.
			if (secondary_.allocate(workload_.tapes.rate, 0)) {
				size_left_ = static_cast<int32_t>(secondary_.tape.size());
			}
			processor_.resetHighPass();
		}
		else {
			std::ignore =
			    primary_.init(workload_.tapes.rate, 0, [](size_t i) { return tape_noise(static_cast<uint32_t>(i)); });
			processor_.resetHighPass();
		}
	}

	/// @brief Renders block @p b into @p out (its first step(b).frames samples), its input written there first.
	/// @return The processor's result; the owner's swap has been made by then.
	DelayBlockResult block(size_t b, std::span<StereoSample> out) {
		const BlockStep s = workload_.step(b);
		const std::span<StereoSample> buffer = out.first(s.frames);
		for (StereoSample& sample : buffer) {
			sample = s.input ? input(k_) : StereoSample{.l = 0, .r = 0};
			++k_;
		}

		DelayTape& primary = primary_.tape;
		DelayTape& secondary = secondary_.tape;
		const int32_t rate = s.settings.rate;

		if (rate != last_rate_) {
			last_rate_ = rate;
			count_ = 0;
		}
		else {
			count_ += buffer.size();
		}
		if (!secondary.isActive()) {
			if (primary.resampling() || static_cast<uint32_t>(rate) != primary.nativeRate()) {
				if (count_ >= (44100 >> 3)) {
					initializeSecondary(rate, true);
				}
				else if (static_cast<uint32_t>(rate) >= (primary.nativeRate() << 1)) {
					initializeSecondary(rate, false);
				}
				else if (static_cast<uint32_t>(rate) < primary.nativeRate() >> 1) {
					initializeSecondary(rate >> 1, false);
				}
			}
		}

		growing_ = secondary.isActive() ? &secondary : nullptr;
		const DelayBlockResult result = processor_.process(buffer, &primary, growing_, s.settings, scratch_);
		if (growing_ != nullptr) {
			size_left_ -= result.secondary_written;
			if (size_left_ < 0) {
				primary_ = std::move(secondary_);
				secondary_ = {};
				swapped_ = true;
			}
			else {
				swapped_ = false;
			}
		}
		else {
			swapped_ = false;
		}
		return result;
	}

	WorkloadRun(const WorkloadRun&) = delete;
	WorkloadRun& operator=(const WorkloadRun&) = delete;

	[[nodiscard]] const Workload& workload() const { return workload_; }
	[[nodiscard]] Processor& processor() { return processor_; }
	[[nodiscard]] const DelayTape& primary() const { return primary_.tape; }
	[[nodiscard]] const DelayTape& secondary() const { return secondary_.tape; }
	/// @return Whether the last block grew a secondary.
	[[nodiscard]] bool grew() const { return growing_ != nullptr; }
	/// @return Whether the last block's secondary was swapped in after it.
	[[nodiscard]] bool swapped() const { return swapped_; }
	[[nodiscard]] int32_t sizeLeft() const { return size_left_; }

private:
	/// Delay::initializeSecondaryBuffer.
	void initializeSecondary(int32_t rate, bool relative) {
		if (!secondary_.allocate(static_cast<uint32_t>(rate), static_cast<uint32_t>(primary_.tape.size()))) {
			return;
		}
		if (relative) {
			primary_.tape.makeNativeRatePreciseRelativeTo(secondary_.tape);
		}
		else {
			primary_.tape.makeNativeRatePrecise();
			secondary_.tape.makeNativeRatePrecise();
		}
		size_left_ = static_cast<int32_t>(secondary_.tape.size()) + 5;
	}

	Workload workload_;
	Buffer primary_;
	Buffer secondary_;
	Processor processor_;
	std::array<StereoSample, kMaxFrames> scratch_{};
	DelayTape* growing_ = nullptr;
	bool swapped_ = false;
	uint32_t k_ = 0;
	uint32_t count_ = 0;
	int32_t last_rate_ = 0;
	int32_t size_left_ = 0;
};

/// @return Whether @p a and @p b hold the same storage, byte for byte (both inactive counts as the same).
[[nodiscard]] inline bool same_storage(const DelayTape& a, const DelayTape& b) {
	const std::span<const std::byte> x = tape_storage(a);
	const std::span<const std::byte> y = tape_storage(b);
	return x.size() == y.size() && (x.empty() || std::memcmp(x.data(), y.data(), x.size()) == 0);
}

/// @return @p tape's read head: its index in the storage, its long position and its last short position; all -1 when
/// the tape is inactive.
[[nodiscard]] inline std::vector<int64_t> head_of(const DelayTape& tape) {
	if (!tape.isActive()) {
		return {-1, -1, -1};
	}
	const DelayTape::Head head = tape.head();
	return {head.pos - tape.begin(), head.long_pos, head.last_short_pos};
}

/// @return Whether @p a and @p b hold the same samples.
[[nodiscard]] inline bool same_frames(std::span<const StereoSample> a, std::span<const StereoSample> b) {
	return std::ranges::equal(a, b, [](StereoSample x, StereoSample y) { return x.l == y.l && x.r == y.r; });
}

/// @return Where @p workload, played through @p A and through @p B, first parts, as "<name> block <n>: <what>" (the
/// output, the result, the owner's swap, a tape's storage or a head); empty when they agree on every block.
template <typename A, typename B>
[[nodiscard]] std::string first_parting(const Workload& workload) {
	WorkloadRun<A> a{workload};
	WorkloadRun<B> b{workload};
	std::array<StereoSample, kMaxFrames> out_a{};
	std::array<StereoSample, kMaxFrames> out_b{};
	for (size_t block = 0; block < workload.blocks; ++block) {
		const DelayBlockResult ra = a.block(block, out_a);
		const DelayBlockResult rb = b.block(block, out_b);
		const std::string at = std::string{workload.name} + " block " + std::to_string(block) + ": ";
		const size_t frames = workload.step(block).frames;
		if (!same_frames(std::span{out_a}.first(frames), std::span{out_b}.first(frames))) {
			return at + "output";
		}
		if (ra.wrapped != rb.wrapped || ra.secondary_written != rb.secondary_written) {
			return at + "result";
		}
		if (a.swapped() != b.swapped() || a.grew() != b.grew()) {
			return at + "lifecycle";
		}
		if (!same_storage(a.primary(), b.primary()) || head_of(a.primary()) != head_of(b.primary())) {
			return at + "primary tape";
		}
		if (!same_storage(a.secondary(), b.secondary()) || head_of(a.secondary()) != head_of(b.secondary())) {
			return at + "secondary tape";
		}
	}
	return {};
}

/// @brief What a workload rendered: each block's output digest, a digest of every block's result, tape sizes and
/// heads, and a digest of both tapes' storage at the end.
struct WorkloadRecord {
	std::vector<uint64_t> outputs;
	uint64_t lifecycle = 0;
	uint64_t tapes = 0;

	bool operator==(const WorkloadRecord&) const = default;
};

inline std::ostream& operator<<(std::ostream& out, const WorkloadRecord& r) {
	out << std::hex << "{outputs";
	for (const uint64_t d : r.outputs) {
		out << " 0x" << d;
	}
	return out << ", lifecycle 0x" << r.lifecycle << ", tapes 0x" << r.tapes << std::dec << '}';
}

/// @brief Folds @p tape's size and read head into @p digest (zeros when inactive).
inline void add_head(Digest& digest, const DelayTape& tape) {
	if (!tape.isActive()) {
		digest.add(uint32_t{0});
		return;
	}
	const DelayTape::Head head = tape.head();
	digest.add(static_cast<uint32_t>(tape.size()));
	digest.add(static_cast<uint32_t>(head.pos - tape.begin()));
	digest.add(head.long_pos);
	digest.add(static_cast<uint32_t>(head.last_short_pos));
	digest.add(tape.nativeRate());
	digest.add(static_cast<uint32_t>(tape.resampling()));
}

/// @return @p workload's record, rendered by @p Processor.
template <typename Processor>
[[nodiscard]] WorkloadRecord record(const Workload& workload) {
	WorkloadRun<Processor> run{workload};
	WorkloadRecord out;
	Digest lifecycle;
	std::array<StereoSample, kMaxFrames> block{};
	for (size_t b = 0; b < workload.blocks; ++b) {
		const DelayBlockResult result = run.block(b, block);
		Digest output;
		output.add(std::span<const StereoSample>{block}.first(workload.step(b).frames));
		out.outputs.push_back(output.value);
		lifecycle.add(static_cast<uint32_t>(result.wrapped));
		lifecycle.add(result.secondary_written);
		add_head(lifecycle, run.primary());
		add_head(lifecycle, run.secondary());
	}
	out.lifecycle = lifecycle.value;
	Digest tapes;
	for (const DelayTape* tape : {&run.primary(), &run.secondary()}) {
		if (tape->isActive()) {
			tapes.add(std::span<const StereoSample>{tape->begin(), tape->end()});
		}
		else {
			tapes.add(uint32_t{0});
		}
	}
	out.tapes = tapes.value;
	return out;
}

/// @brief What today's DelayProcessor renders from each workload, in WorkloadId order: recorded from it, identical on
/// the host and on ARM.
inline const std::array<WorkloadRecord, kWorkloadCount> kRecorded{{
    // native_straight_digital
    {.outputs = {0xdc9335e3aab409b9ull, 0x2494e0baa44182fdull, 0xdb9f39170ffd984dull, 0xb5207d7766ae4d54ull,
                 0x55b4bc64679a47bdull, 0x92c1dcebc90cb68eull, 0x355fb4046b82918dull, 0x852f8ac38faaefd1ull,
                 0x9340c7d7626366ecull, 0xe020e07a89ab3608ull, 0x7ae19cfe1c959223ull, 0x24182662630cbf08ull,
                 0x8cf9856172bb7fb9ull, 0x87c522f3f4412bdfull, 0xec9159b75a62ab39ull, 0xfb05f11cb40cade8ull,
                 0x81234b2e636e5dc5ull, 0xfcc41a4b44d816f6ull, 0xc1359601c7f16f8eull, 0x8974d979085abe25ull,
                 0x4e9c616981d3ff20ull, 0xf7574141c11abf23ull, 0xf4e4163c92e14d63ull, 0x1081bbe6fa19fb96ull,
                 0xbed12c726b308144ull, 0x0d958d18b00b4076ull, 0x5267e248a71dbe7dull, 0x1b1ff29c26977189ull,
                 0x6b8e167aff6fa39cull, 0x75883041aac099b8ull, 0xa4eeaa2ee0d05960ull, 0xacd3e69ed19d520bull,
                 0x8d582da9f28a1d23ull, 0xf725daf6bff29d32ull, 0xb9c063fc2505fd71ull, 0xe647652fd528c177ull,
                 0xef9c248920e40b83ull, 0xec5ffcfd71f216c5ull, 0x693423163532afd0ull, 0x2c7ab11c4177b760ull},
     .lifecycle = 0x59f12cd6565cccbfull,
     .tapes = 0xcbaf21f80fc7908full},
    // native_straight_analog
    {.outputs = {0x53313faa72adcbbdull, 0x266c24b75d5b46beull, 0x96dc1f6fec2b2fa0ull, 0xec9fdcb8fd1e03ccull,
                 0xcdd0e6e495fd64caull, 0x277a2f4d1192b4a6ull, 0x581ce7a3e464bccaull, 0xa2daac097b070c44ull,
                 0x16248c6c6b990cc4ull, 0x8ca7f58721d34515ull, 0x4d979e7b2eb5acf9ull, 0xf4790964370097c0ull,
                 0x6123e90ee5fd0380ull, 0x624a4002ea092224ull, 0x167da88ea5c5b71aull, 0xb3bcedbfc678e878ull,
                 0xaa3e5d33cff40fceull, 0x9e32606239b27d33ull, 0xe3d98434e626c81bull, 0xdf35dffd9022425eull,
                 0xdc7ce9dc2639465cull, 0x5f48f5fd4b5e9ec4ull, 0x1a95a5da8a866a1bull, 0xf6b014b25af6c3a9ull,
                 0x860fef8d89b1fb69ull, 0x1989ae499ed675fdull, 0x396fc1aad3f149c3ull, 0x57c73cc486025178ull,
                 0x7f6665907a1bbff8ull, 0xdb8a5ba8cc9ae876ull, 0x514ba106a5caaccfull, 0xace0cc496dca8f63ull,
                 0xc843a2aa0a7acb27ull, 0xd8ef544ff47a5a93ull, 0x6421f1b6f9a10429ull, 0xd17c280c4575ee41ull,
                 0x90197bdf61062801ull, 0x9ef316384dfd5fabull, 0xf52770ba7504324dull, 0xb8da23c5012de846ull},
     .lifecycle = 0x59f12cd6565cccbfull,
     .tapes = 0xc2836dc2348f28bbull},
    // native_cross_digital
    {.outputs = {0xdc9335e3aab409b9ull, 0x2494e0baa44182fdull, 0xdb9f39170ffd984dull, 0xb5207d7766ae4d54ull,
                 0x55b4bc64679a47bdull, 0x92c1dcebc90cb68eull, 0x355fb4046b82918dull, 0x852f8ac38faaefd1ull,
                 0x9340c7d7626366ecull, 0xe020e07a89ab3608ull, 0x7ae19cfe1c959223ull, 0x24182662630cbf08ull,
                 0x8cf9856172bb7fb9ull, 0x87c522f3f4412bdfull, 0xec9159b75a62ab39ull, 0xf2ec008ca5dfceffull,
                 0xe09dea7a5587b4d1ull, 0x97787a3d4c2f4d0bull, 0x9a6a340bfcc2f685ull, 0xceeec6966ad85ff3ull,
                 0xe9175bc748b451a4ull, 0xd7ae533a23c646b5ull, 0xb9f6142e1ef8a41bull, 0x9136fb8da19fc26dull,
                 0x9cdda3fc41078529ull, 0x1b5a35cc0e47c085ull, 0x35deda4153aeafc0ull, 0x27baea5c7bfb5c41ull,
                 0xaca2ec782290e681ull, 0x53d64ed8ad6adfc7ull, 0xeafd19fc86e2c6f2ull, 0xbf4bf9509b4504b2ull,
                 0x76da5dbef48f2476ull, 0x0ee169f551084e31ull, 0x0af4dedce451209full, 0xe7c1e0ea470e10b9ull,
                 0x09d2f2ce7b14e682ull, 0xb46630c3fc71307cull, 0x813c80df4dc166d0ull, 0x7fc16033a3f40b9aull},
     .lifecycle = 0x59f12cd6565cccbfull,
     .tapes = 0x51925130e9ed5473ull},
    // native_cross_analog
    {.outputs = {0x5ea6ff37bdcddf83ull, 0x2be248fcb7d55b2eull, 0x263421826946d1bcull, 0x8724cf1748834d89ull,
                 0xef81cde1fef92420ull, 0x60cd7538196678d7ull, 0xf6973861201f3c3full, 0x931db91725413a11ull,
                 0x01cba55bd0236699ull, 0xe324107fa4240f9dull, 0xdbe685aee2eecc9bull, 0xfd33ae612f32faebull,
                 0xcf52c03a1375fff8ull, 0x7b4ba05838a37701ull, 0x27fc52cdafc59a87ull, 0x67d5a73220c7b2a1ull,
                 0xe91238cc8d453ec4ull, 0xdba97335938d5e6bull, 0xadf8e686d5d727a7ull, 0x33081472c1713f7dull,
                 0xeb1590c223128309ull, 0xf282a34402f1dfb6ull, 0x60117607b7cb0610ull, 0xd64cb36276104db3ull,
                 0x11af2e259e8cd30bull, 0x23c1fbffe82733f3ull, 0x6b88b4b78e12039eull, 0x1f40ef70ec1d22a0ull,
                 0xfc092d48f8ddf6a3ull, 0x9e54fd05a602fceeull, 0xce7faf5262955556ull, 0x12984538375696ecull,
                 0xc624bd83744e1dbbull, 0x5c1421610b2711c0ull, 0x2d52b2613f944085ull, 0x32bc222bd1abcd4aull,
                 0xc4a74044d16b3bfdull, 0x3ea259de6d61377aull, 0x18bcc3aa2d11e6c8ull, 0xa1e1f551f03b4556ull},
     .lifecycle = 0x59f12cd6565cccbfull,
     .tapes = 0x74ce8c64e70c8bfbull},
    // res_lo_straight_digital
    {.outputs = {0xd242be8bc15a3d54ull, 0x850b1f0dda6dfe95ull, 0x8c2d472d8b84d6afull, 0x4e0d7f83ec3356c0ull,
                 0x197d5c62b33e296eull, 0xf38d257b64087810ull, 0xc0bf3265fd6f70b7ull, 0x59d2cb35c43783d1ull,
                 0x066a76f12de4a58eull, 0x3d405d75143683c1ull, 0x1ed2e7aeadbc0e23ull, 0x3a1eaac35e522cfcull,
                 0xa3707274163d3ed4ull, 0x7bc5ce759f85b5b5ull, 0x1e7b19687358963eull, 0xab72a9ac939350adull,
                 0x0f2e2d12776e5b79ull, 0x9fff7927a5a47515ull, 0x76cba315dd583b88ull, 0x28f9ea53cc16aadeull,
                 0x67c0df422979159cull, 0xbc59d6c2f51e4c6dull, 0x2738d0e11511b8c9ull, 0xd869a6cab819fc18ull,
                 0x0031a0eddfffcfe7ull, 0xb669bfe7f6672f4cull, 0x659560b4033d9b76ull, 0x6f74991bffb6e666ull,
                 0xcc9a9efffea2d22eull, 0xd77b073ffc622581ull, 0xc5afa3763e14e842ull, 0xddd26ae6478e8b2bull,
                 0x4bdfed479e985df8ull, 0xa481e73067c2a9a4ull, 0x5429bb9e140de3feull, 0xeac668a2f0a83fe9ull,
                 0x4738546b3192d1a2ull, 0x51eaa12778bd09a6ull, 0x1cbdf5cb4919371full, 0xcfa1d5c038c9ca59ull},
     .lifecycle = 0x2ab74c5fe9a6e01eull,
     .tapes = 0xaf4b6c18ab960e07ull},
    // res_lo_straight_analog
    {.outputs = {0x62c0489cae2e3a31ull, 0x326f1b82e050f149ull, 0xaf07412439a0b98cull, 0xbbf65c87d1d39609ull,
                 0xa65fedd168f099bbull, 0x513d1e61b83e7a25ull, 0xcc7517757299d9edull, 0xfd67fa5ba89c092bull,
                 0x97691cc2741548dfull, 0xabbf709e6b77c6d8ull, 0x742517afa7a0d5caull, 0x87e99bd0e277dc86ull,
                 0x1c50241ebce6b95cull, 0x0aec6892653867e8ull, 0xcdda6b3fc08ad108ull, 0x2d319c60a701482dull,
                 0x00a5013c7b7de6deull, 0xb5b2fd146891caf4ull, 0x6b0b72a93e62880eull, 0x5fc7088b0f42f409ull,
                 0xa51a905a914831b2ull, 0xaa5572bf09987356ull, 0x33795d3133079be1ull, 0x0b4fccc79a17ec15ull,
                 0x0dc7aa8ab5efb3a7ull, 0x301c8cbe1f0c2194ull, 0x9d50eb3467cdc722ull, 0x746a5ec70d98d40full,
                 0x81393bf1fe9564d4ull, 0x641b25ee58cc3180ull, 0x8e2c20c039036194ull, 0xf2e00929cfc183ebull,
                 0x4fdcefa6d2888b31ull, 0xa18c523b2389e1a9ull, 0x7bcd5c3a83629bc2ull, 0xbd4d956dc70ffdbbull,
                 0x49f416282fc58700ull, 0x28a3f05c2bf7760aull, 0x20d3e4546d0e19b4ull, 0x79a9c8acd7153870ull},
     .lifecycle = 0x2ab74c5fe9a6e01eull,
     .tapes = 0x7fa6a1905a921027ull},
    // res_lo_cross_digital
    {.outputs = {0xd242be8bc15a3d54ull, 0x850b1f0dda6dfe95ull, 0x8c2d472d8b84d6afull, 0x4e0d7f83ec3356c0ull,
                 0x197d5c62b33e296eull, 0xf38d257b64087810ull, 0xc0bf3265fd6f70b7ull, 0x59d2cb35c43783d1ull,
                 0x066a76f12de4a58eull, 0x3d405d75143683c1ull, 0x1ed2e7aeadbc0e23ull, 0x3a1eaac35e522cfcull,
                 0xa3707274163d3ed4ull, 0x7bc5ce759f85b5b5ull, 0x1e7b19687358963eull, 0xab72a9ac939350adull,
                 0x0f2e2d12776e5b79ull, 0x9fff7927a5a47515ull, 0x76cba315dd583b88ull, 0x28f9ea53cc16aadeull,
                 0x67c0df422979159cull, 0x1bb25bfa67cfd608ull, 0x79ef2aca2795b529ull, 0x3c6032f7cb257326ull,
                 0x4228ac7f93a35103ull, 0x97d9f7a76bb4a05dull, 0xaf83f8140fe28e61ull, 0xead5fa69b0e3abf2ull,
                 0x6d156b727b277e26ull, 0x7959508891405ba0ull, 0xb6cfb1b52ab6ac03ull, 0xd99ddf5dd029aec3ull,
                 0x1212dc074cefec1dull, 0xca56643e3159effaull, 0xd34ce2f7ac220bd8ull, 0x67dc8ef7061a7156ull,
                 0x12194ec7632ad5e4ull, 0x5be687366e4b69e4ull, 0xc2fe0d4601d1e0cfull, 0x59bab7ede850a311ull},
     .lifecycle = 0x2ab74c5fe9a6e01eull,
     .tapes = 0x09f21728ab65c723ull},
    // res_lo_cross_analog
    {.outputs = {0xda8ef657d351b8afull, 0xfb5e81164b510149ull, 0x00d5152b2ae49820ull, 0x440f6e76d54c004aull,
                 0xc30b940595bc7df9ull, 0x9186377c8a861ea6ull, 0xf55882418170f7a6ull, 0x0e77e8884357ba9full,
                 0xd2e0e10a2a387b24ull, 0xa5af8155667b4fbdull, 0xf762f250be557230ull, 0x26e24a105077dc96ull,
                 0xaedf056c78cc79fdull, 0xb5fd977253632c0bull, 0x4747b7f3e0676b57ull, 0x6fe892e12f8bc79bull,
                 0x401c467cc9562ed3ull, 0x69bffb729eac2abbull, 0xed245fbfbd3293b0ull, 0xf5d62e427476c515ull,
                 0xa27cb4398aa00909ull, 0x7faf7e04f2b45a59ull, 0x7ed388653b97c515ull, 0x9558ffd10dbcdf29ull,
                 0x0319289aec1952cdull, 0x6f08aee2224fcbc7ull, 0xfe0ffb63c22131b6ull, 0xd4deeaa400d16fccull,
                 0xb28d108b62dfb826ull, 0xf6d3fa5315bd45e1ull, 0x09bc21980d54ca86ull, 0x868673321e415a5aull,
                 0x3dc85e5249457ea0ull, 0x2bf4264832fde1f8ull, 0x3ea3b7dadb5a07acull, 0xc3a42744d783dceaull,
                 0xd52739f2089b7a85ull, 0x931f60c43f8d5359ull, 0x13308456a2e9c7bbull, 0x05837e40923fbd92ull},
     .lifecycle = 0x2ab74c5fe9a6e01eull,
     .tapes = 0xfe171e154c88d0d3ull},
    // res_hi_straight_digital
    {.outputs = {0xcc17e1d500a2c3d8ull, 0x132a75e3973298c1ull, 0xc2f9eb8cbc9107c4ull, 0x257282027d810798ull,
                 0xc5a774b2334690afull, 0x10da6860793a2515ull, 0xa207adbeb379fcbfull, 0x6e700408311d903bull,
                 0xde287053c973edbdull, 0xf4037b06a67b45ffull, 0xee8aab7c9625ff76ull, 0xa88fc1fd6db3fe46ull,
                 0x3b88a8e7e5db9ebbull, 0x5dd4bdc014fc1b6full, 0x544ea60dae3fbafbull, 0xbe57695deff4bc11ull,
                 0xcce4adcac1be098eull, 0xe8ed111d47613269ull, 0x1878c2660d7f06a3ull, 0x1f8cd271a070bf0dull,
                 0xb374de3096cf8be4ull, 0x2c2c47b18eeb82e6ull, 0x10f1f37ce67dce4eull, 0x78dd688bfd46cb27ull,
                 0x9882746823f7398aull, 0x4b74989c89022d5dull, 0x9102aecd19e0c328ull, 0xd7c6096aeda61861ull,
                 0x9d441733b764d136ull, 0x2ee9e291ef4be8d2ull, 0xa3b7c4a82ab68144ull, 0xcc57e32dcb30e15cull,
                 0x3fa4196c7d452b61ull, 0xe2d0c77b204f2e4full, 0x72ebdfdd4d87491aull, 0xb39dcfffe15b4e0dull,
                 0x729596bb7b3ad806ull, 0x72bfbc41293fadfcull, 0x95ede1bf38dcda8dull, 0x4c2276b1628c82aeull},
     .lifecycle = 0x2eda451b5fef29a6ull,
     .tapes = 0x336f94f5f758ea6full},
    // res_hi_straight_analog
    {.outputs = {0x75a490a4157f0ce5ull, 0xb5f53a4c747ecec2ull, 0x4382dc7c9371fda2ull, 0xa2e30e239d299a50ull,
                 0xbc6ce629a70ebe42ull, 0xc71e8a85800ebb40ull, 0xe6694ac9767ea9d1ull, 0x410aabfab9baf98cull,
                 0x99ec0a416844ef90ull, 0x4baf2acbe710c44dull, 0x92fec2857f9c6291ull, 0xaeda08fa97d9a389ull,
                 0xebadfc15b0039a85ull, 0x87c7718bbd4db7b5ull, 0x60dadd830130cde6ull, 0x563e7e27543a7655ull,
                 0x22f26f30791b96a1ull, 0x7a771139c10d971full, 0xb5512e0d7c6150f6ull, 0x389abd0e3f929f59ull,
                 0x4eb528bcc3d91bfeull, 0x24006e59fee8f9eeull, 0x4ecd71a957c6481bull, 0x39341dd01fc52f68ull,
                 0x95065133c1e296a2ull, 0x90e3b025061cae4cull, 0x40179bc8ee4740a7ull, 0xdbb906e4996f0c81ull,
                 0x44ca5eb81ded595cull, 0x1b211478c4dce366ull, 0x9bbb985130d4dcb8ull, 0x9007680a5d930b87ull,
                 0x6934bd0883441181ull, 0xc974336fb997ee3bull, 0x0d3dca39cd2ffad6ull, 0x091b816b585c6583ull,
                 0x333a30d82e41b20aull, 0x858071d8571ee46bull, 0xd5819a75b122a738ull, 0x8b745198f849de9eull},
     .lifecycle = 0x2eda451b5fef29a6ull,
     .tapes = 0x858414c9b8f67017ull},
    // res_hi_cross_digital
    {.outputs = {0xcc17e1d500a2c3d8ull, 0x132a75e3973298c1ull, 0xc2f9eb8cbc9107c4ull, 0x257282027d810798ull,
                 0xc5a774b2334690afull, 0x10da6860793a2515ull, 0xa207adbeb379fcbfull, 0x6e700408311d903bull,
                 0xde287053c973edbdull, 0xf4037b06a67b45ffull, 0x3bf7b20c98406f10ull, 0x850c4bae63023e02ull,
                 0x97a9c225a0510381ull, 0xaab612ab79f97ecbull, 0x66cdaf09f55d68b0ull, 0x9090105315c464faull,
                 0x2b8eb64fd94c1367ull, 0x87c37b51051aab30ull, 0x848e2b93bc559cc0ull, 0xf5c7f78614eafb1bull,
                 0x9f8d3c85cf7e4b03ull, 0xae3ab5634fcf47e0ull, 0x8a925f8f1b8d0a3full, 0x2803ce5f844d21eeull,
                 0x5a2a00f38540cc19ull, 0x459732bcf33e1cfcull, 0x92b01a1270a5b73cull, 0xaac60be2202afaacull,
                 0x6b842ad6040d6f03ull, 0xc61879aaa1f744eaull, 0xd8687f36a66e5d40ull, 0xbc2590a33a8bd5f7ull,
                 0xf973c55e1906732bull, 0x82a371d4a9516d9aull, 0x2e11bd8dc9b79d5bull, 0xa48904582a11a1ffull,
                 0x2b49804892c32105ull, 0x11c1f8c1a49105d2ull, 0xcb94d7fb94db6e02ull, 0x2c28cc45898fad08ull},
     .lifecycle = 0x2eda451b5fef29a6ull,
     .tapes = 0xfe99b9865eadddffull},
    // res_hi_cross_analog
    {.outputs = {0x91a9c3936f01b299ull, 0x3f7afc8216cdd803ull, 0xa2ffedee0390a720ull, 0xc749f98717ce896cull,
                 0x6df57f0c627ac268ull, 0x5cab45f0547bf932ull, 0x86bde1961420650aull, 0x4e98788f87352ab8ull,
                 0x9c3d2e39ed50a7f8ull, 0x573c4187148aca35ull, 0xfc0ed1ade044c276ull, 0x2692a17965da04d3ull,
                 0x0d47b5e6dea2fab7ull, 0x93a412758f842c52ull, 0xadaff43622f348fdull, 0x3c987dc19642d86bull,
                 0x7256a7035efc6c66ull, 0xb617dadafbc820d3ull, 0xc4b3b5f5067284ddull, 0x09380670176080ebull,
                 0xd843dcfaf01e4443ull, 0x8a21af9163619d0full, 0x442d056176e6398eull, 0xe7b41b490afc4cabull,
                 0x924680a161269fc1ull, 0xa4873a8f2ee058b3ull, 0xf2e349fb4116a772ull, 0xef7d83c76c557907ull,
                 0x7a76df9f259a2e77ull, 0x4262e72d44180274ull, 0xf5f3bea7c03cb473ull, 0x37e3cf17600a6d21ull,
                 0x48d945f6c0ef66a0ull, 0xaf9c8c0a1efc112eull, 0x5f95dad1311b0c9bull, 0xa0b28c3bfec30f99ull,
                 0xc853498b84789922ull, 0xb1b8228679153689ull, 0x7378d5795c5ef383ull, 0x7eea5a7acaa764c7ull},
     .lifecycle = 0x2eda451b5fef29a6ull,
     .tapes = 0xaab913fdda093957ull},
    // mono_ping_pong
    {.outputs = {0x75a490a4157f0ce5ull, 0xb5f53a4c747ecec2ull, 0x4382dc7c9371fda2ull, 0xa2e30e239d299a50ull,
                 0xbc6ce629a70ebe42ull, 0xc71e8a85800ebb40ull, 0xe6694ac9767ea9d1ull, 0x410aabfab9baf98cull,
                 0x99ec0a416844ef90ull, 0x4baf2acbe710c44dull, 0x92fec2857f9c6291ull, 0xaeda08fa97d9a389ull,
                 0xebadfc15b0039a85ull, 0x87c7718bbd4db7b5ull, 0x60dadd830130cde6ull, 0x563e7e27543a7655ull,
                 0x708893a371e8c535ull, 0x52baaa9cfca2ef91ull, 0x05e6f646dbb871f8ull, 0x6690000a442e4a2bull,
                 0x119d00f51019301aull, 0x0afe5a51682256faull, 0x5645ba40cd26b915ull, 0x52720552757d3fecull},
     .lifecycle = 0xdb3f26de23fadd57ull,
     .tapes = 0x7a43b7cca704c707ull},
    // rate_sweep
    {.outputs = {0x9778d4dc5a5b2697ull, 0x232c239d1b2b0961ull, 0x44bc6b1fca386366ull, 0x261a024092a39ea5ull,
                 0x8d7d27df5d44f858ull, 0x91f9c971c5c71939ull, 0xd4339b491090d9bbull, 0x41e0371a86bd204cull,
                 0x877577650788daf6ull, 0xb1695a1c3852ab6aull, 0x9d3982515f185722ull, 0xe5003c7f560dfd2bull,
                 0xa7efdcf3c0ec22a0ull, 0x0e28a8dfbbcd8cbbull, 0x0416794ee148f0b5ull, 0x5b6033654dc023e4ull,
                 0xb7458a3213268b48ull, 0x6735b2b1891aabecull, 0x644f5a9f7a6030e0ull, 0x74e7e116d632fe71ull,
                 0x2566bf6af1f068e9ull, 0x8599a6c801fa5afdull, 0x1f736948a3d6602cull, 0x6da93524f0efce4dull,
                 0x785c46f5121c8ea2ull, 0xcc710dadebb89d8eull, 0x02afbb9d5cdc9513ull, 0x6bb5ccdd941776b8ull,
                 0x65f67b788cd18fc5ull, 0xb912eacba50d89e2ull, 0x9c54799ca24c4330ull, 0x681f6a4be4ca51eaull,
                 0xaf1a0e01532f6eaeull, 0x1251831a6883a804ull, 0x4dea6dff683f0f93ull, 0xc383f4a2f4d428dbull,
                 0x302e6fd0687d01e7ull, 0x069b06bf72a086d1ull, 0x17c8a92da3fcad76ull, 0x8aec9586c6929724ull,
                 0x22b9ba04d3d2f173ull},
     .lifecycle = 0x1bdf405908eac5f0ull,
     .tapes = 0x70a207c4512adf1bull},
    // growth_swap
    {.outputs = {0x03b67a8412b45f75ull, 0x22170d6d7b49bfd5ull, 0xb343eadc5667f935ull, 0x4bda5202b37731d5ull,
                 0x1cb46b0ae2dccc75ull, 0xed00d17e8a0a43d5ull, 0xe4cee7972271a2b5ull, 0x53f564ee19e943d5ull,
                 0x519512e43dfaf475ull, 0xf007bb72478f1167ull, 0x524f9e5a192926ceull, 0x7b561f84e8a7c376ull,
                 0x6255e34409991d8aull, 0xc568faf92342c2f3ull, 0xc2a7e73baf8dbf24ull, 0xdca57cf80193ced1ull,
                 0xf3045721b009b4b7ull, 0xa03adb58805db702ull, 0xcc6ae5368ae6782aull, 0x335b8936f4c617f5ull,
                 0x4fe04e4480f54104ull, 0x0fd6da1434a44b35ull, 0x8970d9ee281a562cull, 0xe81120cc489aab74ull,
                 0x4e942078c873453aull, 0x44cd85e26f00ec9full, 0x2f62b263d4d63461ull, 0x54a795255f14875bull,
                 0xdadafc5c091e81fdull, 0xfdfb658bd3bbdb18ull, 0x672a68a0fcab828bull, 0xac73e5d9ab8f33d2ull,
                 0x78e6646b695c4e18ull, 0x9bed52f215ea53b6ull, 0x9219c2637d3593c5ull, 0x1cba33858c321e32ull,
                 0x20ddd854381d050eull, 0xa251d1f526999524ull, 0x57f7e8c13974028eull, 0x2f5099452753525cull,
                 0x436e72c320659904ull, 0x38e9219272d9f652ull, 0xf7b09b835cfc0d7full, 0x329c89639cf6843cull,
                 0x423d67db2ba7ec34ull, 0x15c7127a9bff9145ull, 0x9f2b5eee581d0e16ull, 0xa89cc78ab3ced56full,
                 0xe7fc2a5c7375cfd3ull, 0xc21e47f2846e6a15ull, 0xc9801e616a041b3eull, 0x6f3350be86771e8eull,
                 0xd47935bb0bb6d117ull, 0x69420e27f5995338ull, 0x70ec04a6abf33b28ull, 0x285fce07dbbf10b5ull},
     .lifecycle = 0x57077806575d2870ull,
     .tapes = 0xd08a2e84a11d66dfull},
    // growth_moving
    {.outputs = {0x03b67a8412b45f75ull, 0x22170d6d7b49bfd5ull, 0xb343eadc5667f935ull, 0x4bda5202b37731d5ull,
                 0x1cb46b0ae2dccc75ull, 0x76a97023614a526dull, 0x1775ec4b6af69577ull, 0xb4c9caebf487529dull,
                 0x844ce8355d0551a0ull, 0xe9d74c44682b901bull, 0x9bb11206de30298bull, 0x278ae5aa9150b412ull,
                 0x754e6f03f1e9f94cull, 0x356a315ea3c3e25full, 0x3696b577490cbfc5ull, 0x6915cc20f758a673ull,
                 0x44cfbd3d8f9b3855ull, 0x0373bb75be81d891ull, 0x3b4de97de61796caull, 0xd352af348c69f3c3ull,
                 0xef84e369ca566468ull, 0xcb709ef81bd35a3bull, 0x421653b800a9b6d2ull, 0x12e96d099a2a617bull,
                 0x6e772f4ce794a03eull, 0x395e201a5f75201cull, 0x91791b9d77bf97caull, 0x684ffd77d7a2716dull,
                 0x9f93b81085048563ull, 0xd9c1196011e67f27ull, 0xcdf58e1dae9bffc8ull, 0x2f5a683349fba931ull,
                 0xef1c81788d815a2dull, 0x39bfedbb4dbf2bc6ull, 0x07c9db4fbb8dc06dull, 0xfd68e02de054e820ull,
                 0x7147d0717100bf94ull, 0x4ef94f690c10b439ull, 0xc4d4eb6d74488542ull, 0xc0e6ea416311d853ull,
                 0xec3a4b6065c5bec4ull, 0x6630df39bec312d8ull, 0x220114a88fe4b8a9ull, 0xcb388d49aa2dddebull,
                 0x4efb15702ff15c89ull, 0x9d98538237e9abf5ull, 0x6f7ccea6be5e31aaull, 0x49b2633dd51197a1ull},
     .lifecycle = 0x71774c7338210900ull,
     .tapes = 0x42117cc864c9fe07ull},
    // read_wraps
    {.outputs = {0x5ea6ff37bdcddf83ull, 0x0c8240c5163f09c9ull, 0x7a1c649288c7001bull, 0x956367796fbf6754ull,
                 0xf6178e8fd7383f5cull, 0x777155f951752fc4ull, 0x5401323725a08e48ull, 0x30c62efe013f31efull,
                 0xbe638b42ee7a80f9ull, 0x08c4d36521229617ull, 0xb5a28d01b312fec7ull, 0x08bc2e747830bc3bull,
                 0x193b4058432e1f54ull, 0x4d5637ef968aa984ull, 0xfccf0bb0befc0db6ull, 0xf4920ba568b04aefull,
                 0xc2da47b63115963aull, 0x5aa4fe9b469a1779ull, 0xe0f5e7117733fb7cull, 0x37993ca201cf42d1ull,
                 0xe08fdf8ec81a41cdull, 0x497a7641c80ced1aull, 0x778d331e490c1b1bull, 0x17ac3dda318a24f0ull,
                 0xfadda8344f5b5bd1ull, 0x853a78cda68f4f22ull, 0xfcb58f8de36e3e8bull, 0x4192a9ebf48dc3e6ull,
                 0xbd1e7764843d829cull, 0xb6403813633135ddull, 0x45860852e48c8a13ull, 0xc9fa9dbffdb9e699ull,
                 0x5a1d674474e66d31ull, 0x8ba545409fa19e30ull, 0x21e1ce91879dbc74ull, 0xa5a05e350556b513ull,
                 0x7a5743003053f240ull, 0x6b8ad0f295240ac8ull, 0xbf4d69a0e99963ccull, 0xc1985077993c2625ull,
                 0xf1aa523ddb276b47ull, 0x13a865ac6b8d77d6ull},
     .lifecycle = 0x561c80c4592c556bull,
     .tapes = 0xd10f87b6378630d7ull},
    // first_blocks
    {.outputs = {0x03b67a8412b45f75ull, 0x22170d6d7b49bfd5ull, 0xb343eadc5667f935ull, 0x4bda5202b37731d5ull,
                 0x1cb46b0ae2dccc75ull, 0xed00d17e8a0a43d5ull, 0xe4cee7972271a2b5ull, 0x53f564ee19e943d5ull,
                 0x519512e43dfaf475ull, 0xe980647a85f291d5ull, 0x0430b7d7ad1ab135ull, 0xf7fc0f52ff4327d5ull,
                 0xa1ed982736ca6575ull, 0xf2adaf55227e95d5ull, 0xa27cf5611cdaf1b5ull, 0x8811b0b4d768a1d5ull,
                 0x842f52d68392ad75ull, 0x7b9099ae8dbe869bull, 0x462af012ecb70118ull, 0x7dea76d808a1736bull,
                 0xd6a7fcead8cac421ull, 0x156ad33061139d8full, 0x4d68c6d109ae676bull, 0x9067ee73d44a7fc5ull,
                 0xc5dce75e12f2a44cull, 0x0a8572be621d75f5ull},
     .lifecycle = 0xfeaba26fe6551286ull,
     .tapes = 0x9d5d4051bf3684adull},
    // first_blocks_resampled
    {.outputs = {0x03b67a8412b45f75ull, 0x22170d6d7b49bfd5ull, 0xb343eadc5667f935ull, 0x4bda5202b37731d5ull,
                 0x1cb46b0ae2dccc75ull, 0xed00d17e8a0a43d5ull, 0xe4cee7972271a2b5ull, 0x53f564ee19e943d5ull,
                 0x519512e43dfaf475ull, 0xfd06dba314344982ull, 0xfddd4814deda9015ull, 0x1bc93d76d5d5d804ull,
                 0xa04be56745f5201bull, 0xaacc46e4f09b0f8bull, 0xfacbe9c7d3f29f54ull, 0x4bf227e783293dc8ull,
                 0xc05658ef3b7e8ba7ull, 0x51a7ed73e9848e90ull, 0x9929a7be1027d175ull, 0xb121e0ef53526a55ull},
     .lifecycle = 0xf9b2c63761a97466ull,
     .tapes = 0xd91e5b85008d3b0full},
    // highest_rate
    {.outputs = {0xdc9335e3aab409b9ull, 0x2494e0baa44182fdull, 0xe0fa856ec4308893ull, 0xbfafb0d9326431afull,
                 0x63b8e9adfeb54073ull, 0x5540bd613ac82c56ull, 0x12971bcf6eecb530ull, 0x57f4b87564d8dbf3ull,
                 0x719b3848a82b286bull, 0xc9b7ea7dadfa53deull, 0xe19efcc24510b20full, 0xcd47b9da65b86145ull},
     .lifecycle = 0x58ab70d29efb3a6full,
     .tapes = 0x01c8b0ef9fc71eb3ull},
    // spin_689
    {.outputs = {0x3e775a567baffd6dull, 0xfefde6be86ecc09cull, 0x5959de9f23101d19ull, 0xb736bef321b46cdcull,
                 0x4d51d55f246287e9ull, 0x4f3403cac914abfeull, 0xace310b3725fa685ull, 0x910e7d813077514dull,
                 0x4c9e7393740158b1ull, 0x8a289344e889c472ull},
     .lifecycle = 0xe314191fbfd8f5eeull,
     .tapes = 0x750b26ede3b2386bull},
    // spin_689_secondary
    {.outputs = {0x5ea6ff37bdcddf83ull, 0x54b7317e4c0257dbull, 0x5c578fb354bdb2adull, 0x1401e258720f012bull,
                 0xfbaa4402483a92c9ull, 0xae175b8d80afa427ull, 0xe13eda47262abf99ull, 0xe91096fbd102fc52ull,
                 0x65cb56845df0406cull, 0xf61856b65db3a605ull, 0xd1b82eb6f663a88full, 0x5478a8c13aa6e2d1ull},
     .lifecycle = 0x296e07e033091e43ull,
     .tapes = 0x58c827671b01c20full},
    // spin_255
    {.outputs = {0xb9c3e35401e5e495ull, 0x7547fc5a2ec5cf1bull, 0x7fb9fa083f2737fcull, 0x77423accfe66e6e3ull,
                 0x2f62e0c9dd801d1bull, 0x30ac54953111ee0aull, 0x2487cd51ec2a8c00ull, 0xb2d69516ba18d00aull},
     .lifecycle = 0xd48a1f48e518a51dull,
     .tapes = 0x4372507dce8f4c47ull},
    // spin_floor
    {.outputs = {0x03b67a8412b45f75ull, 0x22170d6d7b49bfd5ull, 0xb343eadc5667f935ull, 0x4bda5202b37731d5ull,
                 0x1cb46b0ae2dccc75ull, 0xed00d17e8a0a43d5ull, 0xe4cee7972271a2b5ull, 0x53f564ee19e943d5ull,
                 0x519512e43dfaf475ull, 0xe980647a85f291d5ull, 0x0430b7d7ad1ab135ull, 0xf7fc0f52ff4327d5ull,
                 0xa1ed982736ca6575ull, 0xf2adaf55227e95d5ull, 0xa27cf5611cdaf1b5ull, 0x8811b0b4d768a1d5ull,
                 0x842f52d68392ad75ull, 0x2f2877f74bd423d5ull, 0x71a067e8a6defb35ull, 0x4f0c48267ff8b9d5ull,
                 0xd80ac658736bb725ull, 0xd80ac658736bb725ull, 0xd80ac658736bb725ull, 0xd80ac658736bb725ull,
                 0xd80ac658736bb725ull, 0xd80ac658736bb725ull, 0xd80ac658736bb725ull, 0xd80ac658736bb725ull,
                 0xd80ac658736bb725ull, 0xd80ac658736bb725ull, 0xd80ac658736bb725ull, 0xd80ac658736bb725ull,
                 0xd80ac658736bb725ull, 0xd80ac658736bb725ull, 0xd80ac658736bb725ull, 0xd80ac658736bb725ull,
                 0xd80ac658736bb725ull, 0xd80ac658736bb725ull, 0xd80ac658736bb725ull, 0xd80ac658736bb725ull},
     .lifecycle = 0xaec6ce20d4808385ull,
     .tapes = 0xcc938f36e0e1161full},
    // native_rate_resampling
    {.outputs = {0x91a9c3936f01b299ull, 0x675dc2baf0c95104ull, 0x3df720af56323702ull, 0x01b27ba983db5581ull,
                 0x6d235bdaee6155c2ull, 0xa677ed643151b8e1ull, 0x387707c445edb4ebull, 0x1097c736581a9375ull,
                 0x6c44bc4febaa3ee2ull, 0x4862064df3034624ull, 0x11edbca896b7abc2ull, 0xa46464f5123c9bf6ull,
                 0xd4ff764471fccf5dull, 0x0e6ec2967ccd7067ull, 0x1ed7b37042db4e5cull, 0xd2fc9f52f7d286f1ull,
                 0x18faec73d14b0ebcull, 0x9cb30e45b846ab19ull, 0x9475e022c3712249ull, 0xb48e5c2321b7cb24ull,
                 0x03c82049717c602dull, 0xfe743b8cba25deabull, 0x50dc3110c9b7e237ull, 0x1426835ef92e4d09ull,
                 0x1425a1f1b8e1892cull, 0xea2e2972bd88fd4eull, 0xa1f955aa36154f4aull, 0x4f6f1359e72ad627ull,
                 0x59ebd19234559a76ull, 0x58ddcf498103f31full},
     .lifecycle = 0x36b53f15598700e4ull,
     .tapes = 0x1614f98723e214d7ull},
    // rate_above_2_31
    {.outputs = {0xdc9335e3aab409b9ull, 0x2494e0baa44182fdull, 0x01293212f190b138ull, 0x695313460e3b8b08ull,
                 0x9e3313e2f79a17fbull, 0x41ab39fd1d243ad1ull, 0xe0851cb70e71cd8bull, 0x490d7aafb52e6533ull,
                 0xd69ac9f4605a2ab6ull, 0xf6c6913f65327c8cull, 0x6b4e312bebf59ae9ull, 0x92b6882aa780fea6ull},
     .lifecycle = 0x3841edc7164ff129ull,
     .tapes = 0x573d0b941f32fab7ull},
}};

} // namespace delay_workloads
