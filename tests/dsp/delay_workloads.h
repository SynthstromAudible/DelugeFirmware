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

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <ostream>
#include <span>
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

} // namespace delay_workloads
