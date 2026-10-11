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
/// @brief The filters' and the compressor's recorded workloads: deterministic runs of blocks, each block's input,
/// settings and events, and what a filter or compressor renders from them.
///
/// A filter workload names its target (one filter, the fade alone, or the whole set), mono or stereo, and per block
/// the frame count, the input's signal, an event taken before the block (a reset, a reset that fades, the set's
/// zeroing), the gain and the set's settings. A target reads only the settings it has: a lone low-pass ladder reads
/// `low` and `low_slot.ladder`, a lone high-pass ladder `high`, a lone SVF `low` and `low_slot.svf`.
///
/// The ladders draw from the synth-wide generator (`CONG`). Every run starts it at @ref kCongSeed, the app's boot
/// value, and keeps its own copy between blocks, so two runs played side by side each see the stream they would see
/// alone; around each block the caller's `jcong` is put back. Each block's draws are counted and their values
/// digested, so a filter that draws one value more or less in a block, or moves a draw into another block, parts on
/// the noise pin. A filter that draws the right values for the wrong samples (R before L, say) parts on the output
/// and state pins, not on the noise pin.
///
/// A run is generic over a family of implementations (@ref ReferenceFamily, or the firmware's in a spec), each
/// providing the filter types, the set's scratch, and a digest of an object's state.

#pragma once

#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/dynamics/rms_feedback_compressor.h" // CompressorKnobs
#include "dsp-next/fixed/filter/filter_modes.h"
#include "dsp-next/fixed/noise/congruential_step.h"
#include "reference_filters.h"
#include "util/waves.h" // jcong

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace filter_workloads {

using deluge::dsp::q31;
using deluge::dsp::StereoSample;
using deluge::dsp::fixed::CompressorKnobs;
using deluge::dsp::fixed::CongruentialStep;
using deluge::dsp::fixed::FilterConfig;
using deluge::dsp::fixed::FilterRoute;
using deluge::dsp::fixed::HighPassSlot;
using deluge::dsp::fixed::LowPassLadderMode;
using deluge::dsp::fixed::LowPassSlot;
using deluge::dsp::fixed::SvfMode;
using filters_reference::Digest;

/// @brief The synth-wide generator's state at boot (util/waves.cpp), where every run starts it.
inline constexpr uint32_t kCongSeed = 380116160;

/// @brief Restores the synth-wide generator to @ref kCongSeed: for a spec that drives a kernel's noise through `jcong`
/// directly rather than through a @ref FilterRun.
inline void reset_cong() {
	jcong = kCongSeed;
}

/// @brief The longest block a workload renders, in frames.
inline constexpr size_t kMaxFrames = 128;

/// @brief What a filter workload runs.
enum class Target : uint8_t {
	LOW_PASS_LADDER,       ///< The low-pass ladder alone, at `low` in `low_slot.ladder`'s mode.
	HIGH_PASS_LADDER,      ///< The high-pass ladder alone, at `high`.
	STATE_VARIABLE_FILTER, ///< The SVF alone, at `low` in `low_slot.svf`'s mode.
	DRY_WET_FADE,          ///< The fade alone, blending the input (dry) with @ref fade_wet of it.
	FILTER_SET,            ///< The whole set.
};

/// @brief A block's input.
enum class Signal : uint8_t {
	SILENCE,    ///< Zeros.
	VOICE,      ///< Pseudo-random, ±2^27: a voice's level.
	LOUD,       ///< ±0x60000000 flipping sign every two samples, with pseudo-random low bits: near full scale.
	FULL_SCALE, ///< Pseudo-random across the whole q31 range.
};

/// @brief What happens to the target before a block is configured.
enum class Event : uint8_t {
	NONE,       ///< Nothing.
	RESET,      ///< A filter's `reset(false)`; the set's `reset()`.
	RESET_FADE, ///< A filter's `reset(true)`; the fade's `begin()`.
	ZERO,       ///< A filter's or the fade's `zero()`.
};

/// @brief One block of a filter workload.
struct FilterStep {
	size_t frames = 1;                            ///< The block's length, in frames: 1 to @ref kMaxFrames.
	Signal signal = Signal::VOICE;                ///< The block's input.
	Event event = Event::NONE;                    ///< Taken before the block is configured.
	q31 gain = 1 << 29;                           ///< The volume going in, Q31.
	FilterConfig low{};                           ///< The low-pass's settings (and a lone SVF's).
	LowPassSlot low_slot{};                       ///< The low-pass slot (a lone ladder's or SVF's mode).
	FilterConfig high{};                          ///< The high-pass's settings.
	HighPassSlot high_slot{};                     ///< The high-pass slot.
	FilterRoute route = FilterRoute::HIGH_TO_LOW; ///< The set's route.
};

/// @brief Every recorded filter workload.
enum class WorkloadId : uint8_t {
	LP12_MONO, ///< Each filter's modes, steady, 40 blocks each, mono and stereo.
	LP12_STEREO,
	LP24_MONO,
	LP24_STEREO,
	LPDRIVE_MONO, ///< The drive ladder below the oversampling threshold.
	LPDRIVE_STEREO,
	LPDRIVE_OS_MONO, ///< The drive ladder oversampling, at cutoff 50,000,000 and resonance 500,000,000.
	LPDRIVE_OS_STEREO,
	LPDRIVE_OS_OFF_MONO, ///< The same settings with oversampling disallowed (CPU load): one draw per sample.
	LPDRIVE_OS_OFF_STEREO,
	LP_MODE_WALK_MONO, ///< T12 → T24 → DRIVE → T12 … across consecutive blocks, never reset.
	LP_MODE_WALK_STEREO,
	LP_OS_TOGGLE_MONO, ///< Oversampling allowed, disallowed, allowed … one toggle per block, never reset.
	LP_OS_TOGGLE_STEREO,
	LP_FADE_MONO, ///< Faded in, restarted mid-fade, then reset and zeroed.
	LP_FADE_STEREO,
	HP_MONO, ///< The high-pass ladder, its saturator bypassed.
	HP_STEREO,
	HP_TANH_MONO, ///< Saturated through tanh.
	HP_TANH_STEREO,
	HP_SATURATED_MONO, ///< Saturated through the antialiased tanh.
	HP_SATURATED_STEREO,
	HP_RESONANCE_SWEEP_MONO, ///< In and out of both saturators between blocks.
	HP_RESONANCE_SWEEP_STEREO,
	HP_FADE_MONO,
	HP_FADE_STEREO,
	SVF_BAND_MONO,
	SVF_BAND_STEREO,
	SVF_NOTCH_MONO,
	SVF_NOTCH_STEREO,
	SVF_MODE_SWITCH_MONO, ///< Band and notch alternating, the morph crossing half.
	SVF_MODE_SWITCH_STEREO,
	SVF_FADE_MONO,
	SVF_FADE_STEREO,
	FADE_MONO, ///< The fade alone: begun, zeroed, begun again and restarted mid-fade.
	FADE_STEREO,
	SET_HL_BOTH_MONO, ///< The set's routes with both filters on.
	SET_HL_BOTH_STEREO,
	SET_LH_BOTH_MONO,
	SET_LH_BOTH_STEREO,
	SET_PAR_BOTH_MONO,
	SET_PAR_BOTH_STEREO,
	SET_PAR_FULL_SCALE_MONO, ///< The parallel route at full scale, an SVF in each slot: the sum wraps.
	SET_PAR_FULL_SCALE_STEREO,
	SET_HL_HP_ONLY_MONO, ///< The route and slot combinations no gate reaches.
	SET_HL_HP_ONLY_STEREO,
	SET_LH_HP_ONLY_MONO,
	SET_LH_HP_ONLY_STEREO,
	SET_LH_NONE_MONO,
	SET_LH_NONE_STEREO,
	SET_LH_LP_ONLY_STEREO,
	SET_PAR_NONE_MONO,
	SET_LP_TAKEOVER_SVF_MODE_MONO, ///< LP off → ladder (fading) → SVF band → SVF notch, both changes mid-fade.
	SET_LP_TAKEOVER_SVF_MODE_STEREO,
	SET_LP_TAKEOVER_BACK_MONO, ///< LP off → ladder (fading) → SVF → ladder, both changes mid-fade.
	SET_LP_TAKEOVER_BACK_STEREO,
	SET_HP_TAKEOVER_SVF_MODE_MONO, ///< HP off → ladder (fading) → SVF band → SVF notch, both changes mid-fade.
	SET_HP_TAKEOVER_SVF_MODE_STEREO,
	SET_HP_TAKEOVER_BACK_MONO, ///< HP off → ladder (fading) → SVF → ladder, both changes mid-fade.
	SET_HP_TAKEOVER_BACK_STEREO,
	SET_FADE_LP_SVF_HP_LADDER_MONO, ///< Both filters switched on from off: the LP SVF and HP ladder fading in.
	SET_FADE_LP_SVF_HP_LADDER_STEREO,
	SET_FADE_LP_LADDER_HP_SVF_MONO, ///< Likewise the LP ladder and HP SVF.
	SET_FADE_LP_LADDER_HP_SVF_STEREO,
	SET_MODE_WALK_MONO, ///< Every slot mode, route and oversampling choice in turn, a reset half way.
	SET_MODE_WALK_STEREO,
	SET_HELD_OFF_MONO, ///< Both ladders on, then each slot OFF for six blocks (its ladder held) and back on.
	SET_HELD_OFF_STEREO,
};

/// @brief How many filter workloads there are.
inline constexpr size_t kWorkloadCount = static_cast<size_t>(WorkloadId::SET_HELD_OFF_STEREO) + 1;

/// @brief A recorded filter workload.
struct FilterWorkload {
	WorkloadId id;
	std::string_view name;
	Target target;
	bool stereo;
	size_t blocks;
	std::function<FilterStep(size_t)> step;

	/// @return The samples in block @p b: one per frame in mono, two (interleaved) in stereo.
	[[nodiscard]] size_t samples(size_t b) const { return step(b).frames * (stereo ? 2 : 1); }
};

namespace detail {

/// @return A well-mixed hash of @p k.
[[nodiscard]] inline uint32_t mix(uint32_t k) {
	k *= 2654435761U;
	k ^= k >> 15;
	k *= 2246822519U;
	k ^= k >> 13;
	return k;
}

/// @return Sample @p k of @p signal.
[[nodiscard]] inline q31 signal_sample(Signal signal, uint32_t k) {
	switch (signal) {
	case Signal::SILENCE:
		return 0;
	case Signal::VOICE:
		return static_cast<q31>(mix(k)) >> 4;
	case Signal::LOUD:
		return (((k / 2) % 2) != 0U ? -0x60000000 : 0x60000000) + (static_cast<q31>(mix(k)) >> 8);
	case Signal::FULL_SCALE:
		return static_cast<q31>(mix(k ^ 0x9E3779B9U));
	}
	return 0;
}

/// The block lengths a workload cycles through: 1 to 9, then longer blocks up to the engine's.
inline constexpr std::array<size_t, 12> kLengths{1, 2, 3, 4, 5, 6, 7, 8, 9, 24, 64, 128};

[[nodiscard]] inline size_t frames_at(size_t b) {
	return kLengths[b % kLengths.size()];
}

/// The blocks of a mode's workload.
inline constexpr size_t kBlocks = 40;

/// @return The gain into block @p b: about 2^29, moving a little.
[[nodiscard]] inline q31 gain_at(size_t b) {
	return (1 << 29) - static_cast<q31>((b % 5) * 1000003);
}

/// @return @p config with its cutoff raised by up to 6/16 across every 7 blocks.
[[nodiscard]] inline FilterConfig swept(FilterConfig config, size_t b) {
	config.frequency += static_cast<q31>(b % 7) * (config.frequency >> 4);
	return config;
}

/// @return A morph (Q29) that crosses the whole range in 9 blocks.
[[nodiscard]] inline q31 morph_at(size_t b) {
	return static_cast<q31>((b % 9) * ((1 << 29) / 8)) - static_cast<q31>(b % 9 == 8);
}

inline constexpr FilterConfig kLow{.frequency = 1 << 23, .resonance = 1 << 28, .morph = 0, .allow_oversampling = true};
/// Cutoff 50,000,000 and resonance 500,000,000: the drive ladder oversamples when allowed.
inline constexpr FilterConfig kBright{
    .frequency = 50000000, .resonance = 500000000, .morph = 0, .allow_oversampling = true};
inline constexpr FilterConfig kHigh{
    .frequency = 1 << 23, .resonance = 50000000, .morph = 1 << 26, .allow_oversampling = true};
/// The high-pass ladder's resonances: its saturator bypassed (processed resonance about 214,000,000), tanh (about
/// 833,000,000, past 750,000,000) and the antialiased tanh (about 1,122,000,000, past 900,000,000).
inline constexpr q31 kHpBypass = 50000000;
inline constexpr q31 kHpTanh = 240000000;
inline constexpr q31 kHpSaturated = 400000000;
inline constexpr FilterConfig kSvf{.frequency = 1 << 24, .resonance = 1 << 28, .morph = 0, .allow_oversampling = true};

inline constexpr LowPassSlot kLowOff{.kind = LowPassSlot::Kind::OFF};
inline constexpr LowPassSlot kLow12{.kind = LowPassSlot::Kind::LADDER, .ladder = LowPassLadderMode::TRANSISTOR_12DB};
inline constexpr LowPassSlot kLow24{.kind = LowPassSlot::Kind::LADDER, .ladder = LowPassLadderMode::TRANSISTOR_24DB};
inline constexpr LowPassSlot kLowDrive{.kind = LowPassSlot::Kind::LADDER,
                                       .ladder = LowPassLadderMode::TRANSISTOR_24DB_DRIVE};
inline constexpr LowPassSlot kLowBand{.kind = LowPassSlot::Kind::SVF, .svf = SvfMode::BAND};
inline constexpr LowPassSlot kLowNotch{.kind = LowPassSlot::Kind::SVF, .svf = SvfMode::NOTCH};
inline constexpr HighPassSlot kHighOff{.kind = HighPassSlot::Kind::OFF};
inline constexpr HighPassSlot kHighLadder{.kind = HighPassSlot::Kind::LADDER};
inline constexpr HighPassSlot kHighBand{.kind = HighPassSlot::Kind::SVF, .svf = SvfMode::BAND};
inline constexpr HighPassSlot kHighNotch{.kind = HighPassSlot::Kind::SVF, .svf = SvfMode::NOTCH};

/// @return A step at block @p b's length and gain.
[[nodiscard]] inline FilterStep at(size_t b) {
	return {.frames = frames_at(b), .gain = gain_at(b)};
}

/// @return The high-pass settings at resonance @p resonance, the cutoff and morph moving.
[[nodiscard]] inline FilterConfig high_at(q31 resonance, size_t b) {
	FilterConfig config = swept(kHigh, b);
	config.resonance = resonance;
	config.morph = (b % 3 == 2) ? 0 : morph_at(b);
	return config;
}

/// @return The SVF settings at block @p b: the cutoff and the morph moving.
[[nodiscard]] inline FilterConfig svf_at(size_t b) {
	FilterConfig config = swept(kSvf, b);
	config.morph = morph_at(b);
	return config;
}

/// A lone low-pass ladder in @p mode at @p config (its cutoff swept when @p sweep), zeroed first.
[[nodiscard]] inline FilterWorkload low_pass(WorkloadId id, std::string_view name, bool stereo, LowPassLadderMode mode,
                                             FilterConfig config, bool sweep) {
	return {.id = id,
	        .name = name,
	        .target = Target::LOW_PASS_LADDER,
	        .stereo = stereo,
	        .blocks = kBlocks,
	        .step = [=](size_t b) {
		        FilterStep s = at(b);
		        s.event = b == 0 ? Event::ZERO : Event::NONE;
		        s.low = sweep ? swept(config, b) : config;
		        s.low.morph = (b % 4 == 3) ? (1 << 27) : 0;
		        s.low_slot = {.kind = LowPassSlot::Kind::LADDER, .ladder = mode};
		        return s;
	        }};
}

/// A lone high-pass ladder at @p resonance, zeroed first.
[[nodiscard]] inline FilterWorkload high_pass(WorkloadId id, std::string_view name, bool stereo, q31 resonance) {
	return {.id = id,
	        .name = name,
	        .target = Target::HIGH_PASS_LADDER,
	        .stereo = stereo,
	        .blocks = kBlocks,
	        .step = [=](size_t b) {
		        FilterStep s = at(b);
		        s.event = b == 0 ? Event::ZERO : Event::NONE;
		        s.high = high_at(resonance, b);
		        return s;
	        }};
}

/// A lone SVF, in @p mode, zeroed first.
[[nodiscard]] inline FilterWorkload svf(WorkloadId id, std::string_view name, bool stereo, SvfMode mode) {
	return {.id = id,
	        .name = name,
	        .target = Target::STATE_VARIABLE_FILTER,
	        .stereo = stereo,
	        .blocks = kBlocks,
	        .step = [=](size_t b) {
		        FilterStep s = at(b);
		        s.event = b == 0 ? Event::ZERO : Event::NONE;
		        s.low = svf_at(b);
		        s.low_slot = {.kind = LowPassSlot::Kind::SVF, .svf = mode};
		        return s;
	        }};
}

/// The fade events a lone filter's fade workload takes: faded in, restarted mid-fade, reset without a fade, zeroed.
[[nodiscard]] inline Event fade_event(size_t b) {
	switch (b) {
	case 0:
	case 5:
		return Event::RESET_FADE;
	case 30:
		return Event::RESET;
	case 33:
		return Event::ZERO;
	default:
		return Event::NONE;
	}
}

/// A whole set, its steps from @p step.
[[nodiscard]] inline FilterWorkload set(WorkloadId id, std::string_view name, bool stereo,
                                        std::function<FilterStep(size_t)> step, size_t blocks = kBlocks) {
	return {.id = id,
	        .name = name,
	        .target = Target::FILTER_SET,
	        .stereo = stereo,
	        .blocks = blocks,
	        .step = std::move(step)};
}

/// A set running @p low_slot and @p high_slot steadily on @p route.
[[nodiscard]] inline FilterWorkload steady_set(WorkloadId id, std::string_view name, bool stereo, LowPassSlot low_slot,
                                               HighPassSlot high_slot, FilterRoute route,
                                               Signal signal = Signal::VOICE) {
	return set(id, name, stereo, [=](size_t b) {
		FilterStep s = at(b);
		s.signal = signal;
		s.low = low_slot.kind == LowPassSlot::Kind::SVF ? svf_at(b) : swept(kLow, b);
		if (low_slot.kind == LowPassSlot::Kind::LADDER && low_slot.ladder == LowPassLadderMode::TRANSISTOR_24DB_DRIVE) {
			s.low = kBright;
		}
		s.low_slot = low_slot;
		s.high =
		    high_slot.kind == HighPassSlot::Kind::SVF ? svf_at(b) : high_at(b % 2 == 0 ? kHpTanh : kHpSaturated, b);
		s.high_slot = high_slot;
		s.route = route;
		return s;
	});
}

/// The low-pass slot taken over mid-fade: off for two blocks, then the 24dB ladder (fading in from dry), then at
/// block 4 the SVF in band mode, then at block 7 @p last (an SVF in notch mode, or the ladder again). The high-pass
/// is off.
[[nodiscard]] inline FilterWorkload low_takeover(WorkloadId id, std::string_view name, bool stereo, LowPassSlot last) {
	return set(id, name, stereo, [=](size_t b) {
		FilterStep s = at(b);
		s.low_slot = b < 2 ? kLowOff : b < 4 ? kLow24 : b < 7 ? kLowBand : last;
		s.low = s.low_slot.kind == LowPassSlot::Kind::SVF ? svf_at(b) : swept(kLow, b);
		s.high_slot = kHighOff;
		s.high = kHigh;
		return s;
	});
}

/// The high-pass slot taken over mid-fade: off for two blocks, then the ladder (fading in from dry), then at block 4
/// the SVF in band mode, then at block 7 @p last (an SVF in notch mode, or the ladder again). The low-pass runs the
/// 24dB ladder throughout.
[[nodiscard]] inline FilterWorkload high_takeover(WorkloadId id, std::string_view name, bool stereo,
                                                  HighPassSlot last) {
	return set(id, name, stereo, [=](size_t b) {
		FilterStep s = at(b);
		s.low_slot = kLow24;
		s.low = swept(kLow, b);
		s.high_slot = b < 2 ? kHighOff : b < 4 ? kHighLadder : b < 7 ? kHighBand : last;
		s.high = s.high_slot.kind == HighPassSlot::Kind::SVF ? svf_at(b) : high_at(kHpTanh, b);
		return s;
	});
}

/// Both slots off for two blocks, then switched on together, each fading in from dry, on @p route.
[[nodiscard]] inline FilterWorkload both_fade(WorkloadId id, std::string_view name, bool stereo, LowPassSlot low_slot,
                                              HighPassSlot high_slot, FilterRoute route) {
	return set(id, name, stereo, [=](size_t b) {
		FilterStep s = at(b);
		s.low_slot = b < 2 ? kLowOff : low_slot;
		s.high_slot = b < 2 ? kHighOff : high_slot;
		s.low = low_slot.kind == LowPassSlot::Kind::SVF ? svf_at(b) : swept(kLow, b);
		s.high = high_slot.kind == HighPassSlot::Kind::SVF ? svf_at(b) : high_at(kHpSaturated, b);
		s.route = route;
		return s;
	});
}

/// Every slot mode, route and oversampling choice in turn, a reset at block 20 and loud blocks among them.
[[nodiscard]] inline FilterWorkload mode_walk(WorkloadId id, std::string_view name, bool stereo) {
	return set(id, name, stereo, [](size_t b) {
		constexpr std::array<LowPassSlot, 7> kLows{kLow12, kLow24, kLowDrive, kLowDrive, kLowBand, kLowNotch, kLowOff};
		constexpr std::array<HighPassSlot, 5> kHighs{kHighLadder, kHighLadder, kHighBand, kHighNotch, kHighOff};
		constexpr std::array<FilterRoute, 3> kRoutes{FilterRoute::HIGH_TO_LOW, FilterRoute::LOW_TO_HIGH,
		                                             FilterRoute::PARALLEL};
		FilterStep s = at(b);
		s.event = b == 20 ? Event::RESET : Event::NONE;
		s.signal = b % 6 == 5 ? Signal::LOUD : Signal::VOICE;
		s.low_slot = kLows[b % kLows.size()];
		s.low = s.low_slot.kind == LowPassSlot::Kind::SVF ? svf_at(b) : kBright;
		s.low.allow_oversampling = (b % kLows.size()) != 3;
		s.low.morph = s.low_slot.kind == LowPassSlot::Kind::SVF ? s.low.morph : (b % 2 == 0 ? 0 : 1 << 27);
		s.high_slot = kHighs[b % kHighs.size()];
		s.high =
		    s.high_slot.kind == HighPassSlot::Kind::SVF ? svf_at(b) : high_at(b % 3 == 0 ? kHpSaturated : kHpTanh, b);
		s.route = kRoutes[b % kRoutes.size()];
		return s;
	});
}

/// The 24dB ladder and the high-pass ladder, high to low, each slot switched OFF for six blocks while the other runs
/// (the low-pass at blocks 10 to 15, the high-pass at 22 to 27), then back on: the switched-off slot still holds its
/// ladder, whose state carries until it comes back on and resets with a fade.
[[nodiscard]] inline FilterWorkload held_off(WorkloadId id, std::string_view name, bool stereo) {
	return set(id, name, stereo, [](size_t b) {
		FilterStep s = at(b);
		s.low_slot = b >= 10 && b < 16 ? kLowOff : kLow24;
		s.low = swept(kLow, b);
		s.high_slot = b >= 22 && b < 28 ? kHighOff : kHighLadder;
		s.high = high_at(kHpTanh, b);
		return s;
	});
}

[[nodiscard]] inline std::vector<FilterWorkload> make_workloads() {
	using enum WorkloadId;
	using LM = LowPassLadderMode;
	std::vector<FilterWorkload> w;
	for (const bool stereo : {false, true}) {
		const auto id = [&](WorkloadId mono) { return static_cast<WorkloadId>(static_cast<uint8_t>(mono) + stereo); };
		const auto name = [&](std::string_view mono, std::string_view st) { return stereo ? st : mono; };
		w.push_back(low_pass(id(LP12_MONO), name("lp12_mono", "lp12_stereo"), stereo, LM::TRANSISTOR_12DB, kLow, true));
		w.push_back(low_pass(id(LP24_MONO), name("lp24_mono", "lp24_stereo"), stereo, LM::TRANSISTOR_24DB, kLow, true));
		w.push_back(low_pass(id(LPDRIVE_MONO), name("lpdrive_mono", "lpdrive_stereo"), stereo,
		                     LM::TRANSISTOR_24DB_DRIVE, kLow, true));
		w.push_back(low_pass(id(LPDRIVE_OS_MONO), name("lpdrive_os_mono", "lpdrive_os_stereo"), stereo,
		                     LM::TRANSISTOR_24DB_DRIVE, kBright, false));
		FilterConfig bright_off = kBright;
		bright_off.allow_oversampling = false;
		w.push_back(low_pass(id(LPDRIVE_OS_OFF_MONO), name("lpdrive_os_off_mono", "lpdrive_os_off_stereo"), stereo,
		                     LM::TRANSISTOR_24DB_DRIVE, bright_off, false));
		w.push_back({.id = id(LP_MODE_WALK_MONO),
		             .name = name("lp_mode_walk_mono", "lp_mode_walk_stereo"),
		             .target = Target::LOW_PASS_LADDER,
		             .stereo = stereo,
		             .blocks = kBlocks,
		             .step = [](size_t b) {
			             constexpr std::array<LowPassLadderMode, 3> kModes{LM::TRANSISTOR_12DB, LM::TRANSISTOR_24DB,
			                                                               LM::TRANSISTOR_24DB_DRIVE};
			             FilterStep s = at(b);
			             s.event = b == 0 ? Event::ZERO : Event::NONE;
			             s.signal = b % 5 == 4 ? Signal::LOUD : Signal::VOICE;
			             s.low = kBright;
			             s.low.morph = (b % 2 == 1) ? (1 << 27) : 0;
			             s.low_slot = {.kind = LowPassSlot::Kind::LADDER, .ladder = kModes[b % kModes.size()]};
			             return s;
		             }});
		w.push_back({.id = id(LP_OS_TOGGLE_MONO),
		             .name = name("lp_os_toggle_mono", "lp_os_toggle_stereo"),
		             .target = Target::LOW_PASS_LADDER,
		             .stereo = stereo,
		             .blocks = kBlocks,
		             .step = [](size_t b) {
			             FilterStep s = at(b);
			             s.event = b == 0 ? Event::ZERO : Event::NONE;
			             s.low = kBright;
			             s.low.allow_oversampling = b % 2 == 0;
			             s.low_slot = kLowDrive;
			             return s;
		             }});
		w.push_back({.id = id(LP_FADE_MONO),
		             .name = name("lp_fade_mono", "lp_fade_stereo"),
		             .target = Target::LOW_PASS_LADDER,
		             .stereo = stereo,
		             .blocks = kBlocks,
		             .step = [](size_t b) {
			             FilterStep s = at(b);
			             s.event = fade_event(b);
			             s.low = swept(kLow, b);
			             s.low_slot = kLow24;
			             return s;
		             }});
		w.push_back(high_pass(id(HP_MONO), name("hp_mono", "hp_stereo"), stereo, kHpBypass));
		w.push_back(high_pass(id(HP_TANH_MONO), name("hp_tanh_mono", "hp_tanh_stereo"), stereo, kHpTanh));
		w.push_back(
		    high_pass(id(HP_SATURATED_MONO), name("hp_saturated_mono", "hp_saturated_stereo"), stereo, kHpSaturated));
		w.push_back({.id = id(HP_RESONANCE_SWEEP_MONO),
		             .name = name("hp_resonance_sweep_mono", "hp_resonance_sweep_stereo"),
		             .target = Target::HIGH_PASS_LADDER,
		             .stereo = stereo,
		             .blocks = kBlocks,
		             .step = [](size_t b) {
			             constexpr std::array<q31, 4> kResonances{kHpBypass, kHpTanh, kHpSaturated, kHpTanh};
			             FilterStep s = at(b);
			             s.event = b == 0 ? Event::ZERO : Event::NONE;
			             s.signal = b % 5 == 4 ? Signal::LOUD : Signal::VOICE;
			             s.high = high_at(kResonances[b % kResonances.size()], b);
			             return s;
		             }});
		w.push_back({.id = id(HP_FADE_MONO),
		             .name = name("hp_fade_mono", "hp_fade_stereo"),
		             .target = Target::HIGH_PASS_LADDER,
		             .stereo = stereo,
		             .blocks = kBlocks,
		             .step = [](size_t b) {
			             FilterStep s = at(b);
			             s.event = fade_event(b);
			             s.high = high_at(kHpSaturated, b);
			             return s;
		             }});
		w.push_back(svf(id(SVF_BAND_MONO), name("svf_band_mono", "svf_band_stereo"), stereo, SvfMode::BAND));
		w.push_back(svf(id(SVF_NOTCH_MONO), name("svf_notch_mono", "svf_notch_stereo"), stereo, SvfMode::NOTCH));
		w.push_back({.id = id(SVF_MODE_SWITCH_MONO),
		             .name = name("svf_mode_switch_mono", "svf_mode_switch_stereo"),
		             .target = Target::STATE_VARIABLE_FILTER,
		             .stereo = stereo,
		             .blocks = kBlocks,
		             .step = [](size_t b) {
			             FilterStep s = at(b);
			             s.event = b == 0 ? Event::ZERO : Event::NONE;
			             s.signal = b % 5 == 4 ? Signal::LOUD : Signal::VOICE;
			             s.low = svf_at(b);
			             s.low_slot = b % 2 == 0 ? kLowBand : kLowNotch;
			             return s;
		             }});
		w.push_back({.id = id(SVF_FADE_MONO),
		             .name = name("svf_fade_mono", "svf_fade_stereo"),
		             .target = Target::STATE_VARIABLE_FILTER,
		             .stereo = stereo,
		             .blocks = kBlocks,
		             .step = [](size_t b) {
			             FilterStep s = at(b);
			             s.event = fade_event(b);
			             s.low = svf_at(b);
			             s.low_slot = kLowBand;
			             return s;
		             }});
		w.push_back(
		    {.id = id(FADE_MONO),
		     .name = name("fade_mono", "fade_stereo"),
		     .target = Target::DRY_WET_FADE,
		     .stereo = stereo,
		     .blocks = kBlocks,
		     .step = [](size_t b) {
			     FilterStep s = at(b);
			     s.event = b == 0 || b == 14 || b == 22 ? Event::RESET_FADE : b == 12 ? Event::ZERO : Event::NONE;
			     return s;
		     }});
		w.push_back(steady_set(id(SET_HL_BOTH_MONO), name("set_hl_both_mono", "set_hl_both_stereo"), stereo, kLow24,
		                       kHighLadder, FilterRoute::HIGH_TO_LOW));
		w.push_back(steady_set(id(SET_LH_BOTH_MONO), name("set_lh_both_mono", "set_lh_both_stereo"), stereo, kLow12,
		                       kHighBand, FilterRoute::LOW_TO_HIGH));
		w.push_back(steady_set(id(SET_PAR_BOTH_MONO), name("set_par_both_mono", "set_par_both_stereo"), stereo,
		                       kLowNotch, kHighLadder, FilterRoute::PARALLEL));
		w.push_back(steady_set(id(SET_PAR_FULL_SCALE_MONO),
		                       name("set_par_full_scale_mono", "set_par_full_scale_stereo"), stereo, kLowNotch,
		                       kHighNotch, FilterRoute::PARALLEL, Signal::FULL_SCALE));
		w.push_back(steady_set(id(SET_HL_HP_ONLY_MONO), name("set_hl_hp_only_mono", "set_hl_hp_only_stereo"), stereo,
		                       kLowOff, kHighLadder, FilterRoute::HIGH_TO_LOW));
		w.push_back(steady_set(id(SET_LH_HP_ONLY_MONO), name("set_lh_hp_only_mono", "set_lh_hp_only_stereo"), stereo,
		                       kLowOff, kHighNotch, FilterRoute::LOW_TO_HIGH));
		w.push_back(steady_set(id(SET_LH_NONE_MONO), name("set_lh_none_mono", "set_lh_none_stereo"), stereo, kLowOff,
		                       kHighOff, FilterRoute::LOW_TO_HIGH));
		if (stereo) {
			w.push_back(steady_set(SET_LH_LP_ONLY_STEREO, "set_lh_lp_only_stereo", true, kLow24, kHighOff,
			                       FilterRoute::LOW_TO_HIGH));
		}
		else {
			w.push_back(
			    steady_set(SET_PAR_NONE_MONO, "set_par_none_mono", false, kLowOff, kHighOff, FilterRoute::PARALLEL));
		}
		w.push_back(low_takeover(id(SET_LP_TAKEOVER_SVF_MODE_MONO),
		                         name("set_lp_takeover_svf_mode_mono", "set_lp_takeover_svf_mode_stereo"), stereo,
		                         kLowNotch));
		w.push_back(low_takeover(id(SET_LP_TAKEOVER_BACK_MONO),
		                         name("set_lp_takeover_back_mono", "set_lp_takeover_back_stereo"), stereo, kLow24));
		w.push_back(high_takeover(id(SET_HP_TAKEOVER_SVF_MODE_MONO),
		                          name("set_hp_takeover_svf_mode_mono", "set_hp_takeover_svf_mode_stereo"), stereo,
		                          kHighNotch));
		w.push_back(high_takeover(id(SET_HP_TAKEOVER_BACK_MONO),
		                          name("set_hp_takeover_back_mono", "set_hp_takeover_back_stereo"), stereo,
		                          kHighLadder));
		w.push_back(both_fade(id(SET_FADE_LP_SVF_HP_LADDER_MONO),
		                      name("set_fade_lp_svf_hp_ladder_mono", "set_fade_lp_svf_hp_ladder_stereo"), stereo,
		                      kLowBand, kHighLadder, FilterRoute::HIGH_TO_LOW));
		w.push_back(both_fade(id(SET_FADE_LP_LADDER_HP_SVF_MONO),
		                      name("set_fade_lp_ladder_hp_svf_mono", "set_fade_lp_ladder_hp_svf_stereo"), stereo,
		                      kLow24, kHighNotch, FilterRoute::LOW_TO_HIGH));
		w.push_back(mode_walk(id(SET_MODE_WALK_MONO), name("set_mode_walk_mono", "set_mode_walk_stereo"), stereo));
		w.push_back(held_off(id(SET_HELD_OFF_MONO), name("set_held_off_mono", "set_held_off_stereo"), stereo));
	}
	std::ranges::sort(w, {}, [](const FilterWorkload& x) { return static_cast<uint8_t>(x.id); });
	return w;
}

} // namespace detail

/// @return Every filter workload, in @ref WorkloadId order.
[[nodiscard]] inline const std::vector<FilterWorkload>& all_workloads() {
	static const std::vector<FilterWorkload> kAll = detail::make_workloads();
	return kAll;
}

/// @return Workload @p id.
[[nodiscard]] inline const FilterWorkload& filter_workload(WorkloadId id) {
	return all_workloads().at(static_cast<size_t>(id));
}

/// @return Block @p b's input: interleaved L/R in stereo.
[[nodiscard]] inline std::vector<q31> filter_input(const FilterWorkload& workload, size_t b) {
	const FilterStep s = workload.step(b);
	std::vector<q31> block(workload.samples(b));
	for (size_t i = 0; i < block.size(); ++i) {
		block[i] = detail::signal_sample(s.signal, static_cast<uint32_t>((b * 1024) + i));
	}
	return block;
}

/// @return What the fade-alone workloads blend toward from the dry sample @p dry.
[[nodiscard]] constexpr q31 fade_wet(q31 dry) {
	return ~dry;
}

/// @return How many steps of the generator lead from @p from to @p to, at most @p limit (UINT32_MAX when more),
/// with each value drawn on the way folded into @p values.
[[nodiscard]] inline uint32_t draws_between(uint32_t from, uint32_t to, uint32_t limit, Digest& values) {
	uint32_t n = 0;
	for (uint32_t x = from; x != to; ++n) {
		if (n == limit) {
			return UINT32_MAX;
		}
		x = CongruentialStep::next(x);
		values.add(x);
	}
	return n;
}

/// @brief The family of reference implementations (reference_filters.h), as @ref FilterRun and @ref CompressorRun take
/// a family: the five filter types and the compressor, the set's scratch built from the fade's and the parallel
/// route's spans, and `digest`, which folds an object's whole state into a @ref Digest.
struct ReferenceFamily {
	using LowPassLadder = filters_reference::ReferenceLowPassLadder;
	using HighPassLadder = filters_reference::ReferenceHighPassLadder;
	using StateVariableFilter = filters_reference::ReferenceStateVariableFilter;
	using DryWetFade = filters_reference::ReferenceDryWetFade;
	using FilterSet = filters_reference::ReferenceFilterSet;
	using Compressor = filters_reference::ReferenceRmsFeedbackCompressor;

	[[nodiscard]] static filters_reference::FilterScratch filter_scratch(std::span<q31> fade, std::span<q31> parallel) {
		return {.fade = fade, .parallel = parallel};
	}

	template <typename T>
	static void digest(const T& object, Digest& d) {
		object.digest(d);
	}
};

/// @brief What one block of a run did.
struct BlockResult {
	q31 gain = 0;       ///< The gain the target returned (0 for the fade alone).
	uint32_t draws = 0; ///< How many values the block drew from the generator.
	uint32_t cong = 0;  ///< The generator after the block.
};

/// @brief One run of a filter workload through @p Family's target: block by block, its own generator state kept
/// between blocks.
template <typename Family>
class FilterRun {
public:
	explicit FilterRun(const FilterWorkload& workload) : workload_{workload} {}

	/// @brief Renders block @p b into @p out (resized to the block), the noise values it draws folded into
	/// @p noise.
	BlockResult block(size_t b, std::vector<q31>& out, Digest& noise) {
		const FilterStep s = workload_.step(b);
		out = filter_input(workload_, b);
		const std::span<q31> fade{fade_scratch_.data(), out.size()};
		const std::span<q31> parallel{parallel_scratch_};
		const uint32_t outer_cong = jcong;
		jcong = cong_;
		BlockResult result{};
		switch (workload_.target) {
		case Target::LOW_PASS_LADDER:
			event(lp_, s.event);
			result.gain = lp_.configure(s.gain, s.low, s.low_slot.ladder);
			render(lp_, out, fade);
			break;
		case Target::HIGH_PASS_LADDER:
			event(hp_, s.event);
			result.gain = hp_.configure(s.gain, s.high);
			render(hp_, out, fade);
			break;
		case Target::STATE_VARIABLE_FILTER:
			event(svf_, s.event);
			result.gain = svf_.configure(s.gain, s.low, s.low_slot.svf);
			render(svf_, out, fade);
			break;
		case Target::DRY_WET_FADE: {
			if (s.event == Event::RESET_FADE) {
				fade_.begin();
			}
			else if (s.event == Event::ZERO) {
				fade_.zero();
			}
			const bool fading = fade_.active();
			if (fading) {
				fade_.keepDry(out, fade);
			}
			for (q31& sample : out) {
				sample = fade_wet(sample);
			}
			if (fading) {
				fade_.blend(out, fade);
			}
			break;
		}
		case Target::FILTER_SET:
			if (s.event == Event::RESET) {
				set_.reset();
			}
			result.gain = set_.configure(s.gain, s.low, s.low_slot, s.high, s.high_slot, s.route);
			if (workload_.stereo) {
				set_.renderStereo(out, Family::filter_scratch(fade, parallel));
			}
			else {
				set_.renderMono(out, Family::filter_scratch(fade, parallel));
			}
			break;
		}
		result.draws = draws_between(cong_, jcong, (4 * static_cast<uint32_t>(out.size())) + 4, noise);
		cong_ = jcong;
		jcong = outer_cong;
		result.cong = cong_;
		return result;
	}

	/// @return The set, between blocks (a FILTER_SET workload's target).
	[[nodiscard]] const typename Family::FilterSet& filterSet() const { return set_; }

	/// @return The digest of the target's state.
	[[nodiscard]] uint64_t state() const {
		Digest d;
		switch (workload_.target) {
		case Target::LOW_PASS_LADDER:
			Family::digest(lp_, d);
			break;
		case Target::HIGH_PASS_LADDER:
			Family::digest(hp_, d);
			break;
		case Target::STATE_VARIABLE_FILTER:
			Family::digest(svf_, d);
			break;
		case Target::DRY_WET_FADE:
			Family::digest(fade_, d);
			break;
		case Target::FILTER_SET:
			Family::digest(set_, d);
			break;
		}
		return d.value;
	}

private:
	template <typename Filter>
	static void event(Filter& filter, Event e) {
		switch (e) {
		case Event::NONE:
			break;
		case Event::RESET:
			filter.reset(false);
			break;
		case Event::RESET_FADE:
			filter.reset(true);
			break;
		case Event::ZERO:
			filter.zero();
			break;
		}
	}

	template <typename Filter>
	void render(Filter& filter, std::span<q31> block, std::span<q31> fade) {
		if (workload_.stereo) {
			filter.renderStereo(block, fade);
		}
		else {
			filter.renderMono(block, fade);
		}
	}

	const FilterWorkload& workload_;
	typename Family::LowPassLadder lp_{};
	typename Family::HighPassLadder hp_{};
	typename Family::StateVariableFilter svf_{};
	typename Family::DryWetFade fade_{};
	typename Family::FilterSet set_{};
	std::array<q31, 2 * kMaxFrames> fade_scratch_{};
	std::array<q31, 2 * kMaxFrames> parallel_scratch_{};
	uint32_t cong_ = kCongSeed;
};

/// @brief Where two runs of a workload first part: the block, and what parted there.
struct Parting {
	size_t block;
	std::string what; ///< "output", "gain", "noise" or "state".

	bool operator==(const Parting&) const = default;
};

/// @return Where @p workload, played through family @p A and through family @p B, first parts; nothing when they
/// agree on every block's output, gain, draws and state.
template <typename A, typename B>
[[nodiscard]] std::optional<Parting> first_parting(const FilterWorkload& workload) {
	FilterRun<A> a{workload};
	FilterRun<B> b{workload};
	std::vector<q31> out_a;
	std::vector<q31> out_b;
	Digest noise_a;
	Digest noise_b;
	for (size_t block = 0; block < workload.blocks; ++block) {
		const BlockResult ra = a.block(block, out_a, noise_a);
		const BlockResult rb = b.block(block, out_b, noise_b);
		if (out_a != out_b) {
			return Parting{.block = block, .what = "output"};
		}
		if (ra.gain != rb.gain) {
			return Parting{.block = block, .what = "gain"};
		}
		if (ra.draws != rb.draws || ra.cong != rb.cong) {
			return Parting{.block = block, .what = "noise"};
		}
		if (a.state() != b.state()) {
			return Parting{.block = block, .what = "state"};
		}
	}
	return std::nullopt;
}

/// @brief What a workload rendered: each block's output digest (its samples, then the gain), each block's draws, a
/// digest of every value drawn in order, and the digest of the target's state at the end.
struct WorkloadRecord {
	std::vector<uint64_t> outputs;
	std::vector<uint32_t> draws;
	uint64_t noise = 0;
	uint64_t state = 0;

	bool operator==(const WorkloadRecord&) const = default;
};

inline std::ostream& operator<<(std::ostream& out, const WorkloadRecord& r) {
	out << std::hex << "{outputs";
	for (const uint64_t d : r.outputs) {
		out << " 0x" << d;
	}
	out << std::dec << ", draws";
	for (const uint32_t n : r.draws) {
		out << ' ' << n;
	}
	return out << std::hex << ", noise 0x" << r.noise << ", state 0x" << r.state << std::dec << '}';
}

/// @return @p workload's record, rendered by @p Family.
template <typename Family>
[[nodiscard]] WorkloadRecord record(const FilterWorkload& workload) {
	FilterRun<Family> run{workload};
	WorkloadRecord out;
	Digest noise;
	std::vector<q31> block;
	for (size_t b = 0; b < workload.blocks; ++b) {
		const BlockResult result = run.block(b, block, noise);
		Digest output;
		output.add(std::span<const q31>{block});
		output.add(result.gain);
		out.outputs.push_back(output.value);
		out.draws.push_back(result.draws);
		noise.add(result.draws);
	}
	out.noise = noise.value;
	out.state = run.state();
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// The compressor.

/// @brief Whose configuration a compressor workload plays.
enum class Owner : uint8_t {
	MASTER, ///< The song's master: makeup 0.85, the threshold its knob, rendered at the master's channel gains.
	SOUND,  ///< A sound: the threshold a param set after the knobs, rendered at neutral volume.
	CLIP,   ///< A clip's global effectable: likewise, at its own volume.
};

/// @brief One block of a compressor workload.
struct CompressorStep {
	size_t frames = 1;
	Signal signal = Signal::VOICE;
	bool render = true; ///< Whether the owner renders; else it resets, as an owner does with its threshold at 0.
	CompressorKnobs knobs{};
	q31 threshold = 0; ///< The threshold param, Q31 of the knob's travel (SOUND and CLIP); MASTER takes the knobs'.
	q31 volume_l = 1 << 27;     ///< MASTER's left channel gain, 4.27 signed.
	q31 volume_r = 1 << 27;     ///< MASTER's right channel gain, 4.27 signed.
	q31 final_volume = 1 << 27; ///< The final volume: 3.29 as MASTER passes it; as SOUND and CLIP pass it, before the
	                            ///< neutral render's >> 3.
};

/// @brief Every recorded compressor workload.
enum class CompressorWorkloadId : uint8_t {
	MASTER_SILENCE_THEN_SOUND, ///< On from the start: seeded from a silent frame, then sound arrives.
	MASTER_ONSET_PARTIAL_WET,  ///< Off through silence, on at a sounding first frame; off and on again while sounding.
	SOUND_SILENCE_THEN_SOUND,
	SOUND_ONSET_PARTIAL_WET,
	CLIP_SILENCE_THEN_SOUND,
	CLIP_ONSET_PARTIAL_WET,
	MASTER_VOLUME_STEPS, ///< The master's final volume and channel gains stepped mid-run: the makeup envelope moves.
};

/// @brief How many compressor workloads there are.
inline constexpr size_t kCompressorWorkloadCount = static_cast<size_t>(CompressorWorkloadId::MASTER_VOLUME_STEPS) + 1;

/// @brief A recorded compressor workload.
struct CompressorWorkload {
	CompressorWorkloadId id;
	std::string_view name;
	Owner owner;
	size_t blocks;
	std::function<CompressorStep(size_t)> step;
};

namespace detail {

/// The knobs an owner's workload starts from.
[[nodiscard]] inline CompressorKnobs owner_knobs(Owner owner) {
	switch (owner) {
	case Owner::MASTER:
		return {.attack = 5 << 24, .release = 5 << 24, .threshold = 1 << 30, .ratio = 64 << 24};
	case Owner::SOUND:
		return {.attack = 3 << 24, .release = 20 << 24, .threshold = 0, .ratio = 100 << 24, .sidechain_hpf = 1 << 30};
	case Owner::CLIP:
		return {.attack = 40 << 24, .release = 2 << 24, .threshold = 0, .ratio = 20 << 24, .sidechain_hpf = 1 << 28};
	}
	return {};
}

/// A step of @p owner's configuration at block @p b.
[[nodiscard]] inline CompressorStep owner_step(Owner owner, size_t b) {
	CompressorStep s{.frames = frames_at(b), .knobs = owner_knobs(owner)};
	switch (owner) {
	case Owner::MASTER:
		s.volume_l = (1 << 27) + 4194304;
		s.volume_r = (1 << 27) - 8388608;
		s.final_volume = 1 << 27;
		break;
	case Owner::SOUND:
		s.threshold = 1 << 29;
		s.final_volume = 1 << 29;
		break;
	case Owner::CLIP:
		s.threshold = 3 << 28;
		s.final_volume = 1 << 30;
		break;
	}
	return s;
}

/// The wet level of the partial-wet workloads: about 60%.
inline constexpr q31 kPartialWet = 1288490188;

[[nodiscard]] inline std::vector<CompressorWorkload> make_compressor_workloads() {
	std::vector<CompressorWorkload> w;
	for (const Owner owner : {Owner::MASTER, Owner::SOUND, Owner::CLIP}) {
		const auto base = static_cast<uint8_t>(2 * static_cast<uint8_t>(owner));
		constexpr std::array<std::string_view, 3> kSilence{
		    "comp_master_silence_then_sound", "comp_sound_silence_then_sound", "comp_clip_silence_then_sound"};
		constexpr std::array<std::string_view, 3> kOnset{"comp_master_onset_partial_wet",
		                                                 "comp_sound_onset_partial_wet", "comp_clip_onset_partial_wet"};
		w.push_back({.id = static_cast<CompressorWorkloadId>(base),
		             .name = kSilence.at(static_cast<size_t>(owner)),
		             .owner = owner,
		             .blocks = kBlocks,
		             .step = [owner](size_t b) {
			             CompressorStep s = owner_step(owner, b);
			             s.signal = b < 4 ? Signal::SILENCE : b % 7 == 6 ? Signal::LOUD : Signal::VOICE;
			             return s;
		             }});
		w.push_back({.id = static_cast<CompressorWorkloadId>(base + 1),
		             .name = kOnset.at(static_cast<size_t>(owner)),
		             .owner = owner,
		             .blocks = kBlocks,
		             .step = [owner](size_t b) {
			             CompressorStep s = owner_step(owner, b);
			             s.knobs.blend = kPartialWet;
			             if (b >= 25) {
				             s.knobs.attack = 10 << 24;
				             s.knobs.release = 60 << 24;
			             }
			             s.signal = b < 3 ? Signal::SILENCE : b % 5 == 4 ? Signal::LOUD : Signal::VOICE;
			             s.render = !(b < 3 || b == 16 || b == 17);
			             return s;
		             }});
	}
	w.push_back({.id = CompressorWorkloadId::MASTER_VOLUME_STEPS,
	             .name = "comp_master_volume_steps",
	             .owner = Owner::MASTER,
	             .blocks = kBlocks,
	             .step = [](size_t b) {
		             CompressorStep s = owner_step(Owner::MASTER, b);
		             s.signal = b % 7 == 6 ? Signal::LOUD : Signal::VOICE;
		             // Up, down past the start, then near silence: the makeup envelope rises and falls.
		             s.final_volume = b < 10 ? 1 << 27 : b < 20 ? 0x7FFFFFFF : b < 30 ? 1 << 24 : 1 << 12;
		             if (b >= 20) {
			             s.volume_l = 1 << 26;
		             }
		             return s;
	             }});
	return w;
}

} // namespace detail

/// @return Every compressor workload, in @ref CompressorWorkloadId order.
[[nodiscard]] inline const std::vector<CompressorWorkload>& all_compressor_workloads() {
	static const std::vector<CompressorWorkload> kAll = detail::make_compressor_workloads();
	return kAll;
}

/// @return Block @p b's input.
[[nodiscard]] inline std::vector<StereoSample> compressor_input(const CompressorWorkload& workload, size_t b) {
	const CompressorStep s = workload.step(b);
	std::vector<StereoSample> block(s.frames);
	for (size_t i = 0; i < block.size(); ++i) {
		const auto k = static_cast<uint32_t>((b * 1024) + (2 * i));
		block[i] = {.l = detail::signal_sample(s.signal, k), .r = detail::signal_sample(s.signal, k + 1)};
	}
	return block;
}

/// @brief One run of a compressor workload through @p Family's compressor, as its owner drives it.
template <typename Family>
class CompressorRun {
public:
	explicit CompressorRun(const CompressorWorkload& workload) : workload_{workload} {
		if (workload_.owner == Owner::MASTER) {
			compressor_.setBaseGain(0.85F);
		}
	}

	/// @brief Renders block @p b into @p out (resized to the block); a block the owner does not render is left as
	/// its input.
	/// @return The gain reduction the compressor reports after the block.
	uint8_t block(size_t b, std::vector<StereoSample>& out) {
		const CompressorStep s = workload_.step(b);
		out = compressor_input(workload_, b);
		compressor_.configure(s.knobs);
		if (workload_.owner != Owner::MASTER) {
			compressor_.setThreshold(s.threshold);
		}
		if (!s.render) {
			compressor_.reset();
			return compressor_.gainReduction();
		}
		const std::span<StereoSample> scratch{scratch_.data(), out.size()};
		if (workload_.owner == Owner::MASTER) {
			compressor_.render(out, s.volume_l, s.volume_r, s.final_volume, scratch);
		}
		else {
			compressor_.renderVolumeNeutral(out, s.final_volume, scratch);
		}
		return compressor_.gainReduction();
	}

	/// @return The digest of the compressor's state.
	[[nodiscard]] uint64_t state() const {
		Digest d;
		Family::digest(compressor_, d);
		return d.value;
	}

private:
	const CompressorWorkload& workload_;
	typename Family::Compressor compressor_{};
	std::array<StereoSample, kMaxFrames> scratch_{};
};

/// @return Where @p workload, played through family @p A and through family @p B, first parts.
template <typename A, typename B>
[[nodiscard]] std::optional<Parting> first_parting(const CompressorWorkload& workload) {
	CompressorRun<A> a{workload};
	CompressorRun<B> b{workload};
	std::vector<StereoSample> out_a;
	std::vector<StereoSample> out_b;
	for (size_t block = 0; block < workload.blocks; ++block) {
		const uint8_t ra = a.block(block, out_a);
		const uint8_t rb = b.block(block, out_b);
		if (!std::ranges::equal(out_a, out_b,
		                        [](StereoSample x, StereoSample y) { return x.l == y.l && x.r == y.r; })) {
			return Parting{.block = block, .what = "output"};
		}
		if (ra != rb) {
			return Parting{.block = block, .what = "gain"};
		}
		if (a.state() != b.state()) {
			return Parting{.block = block, .what = "state"};
		}
	}
	return std::nullopt;
}

/// @return @p workload's record, rendered by @p Family: each block's output digest (its samples, then the gain
/// reduction) and the state at the end. Nothing draws noise.
template <typename Family>
[[nodiscard]] WorkloadRecord record(const CompressorWorkload& workload) {
	CompressorRun<Family> run{workload};
	WorkloadRecord out;
	std::vector<StereoSample> block;
	for (size_t b = 0; b < workload.blocks; ++b) {
		const uint8_t reduction = run.block(b, block);
		Digest output;
		output.add(std::span<const StereoSample>{block});
		output.add(reduction);
		out.outputs.push_back(output.value);
	}
	out.state = run.state();
	return out;
}

} // namespace filter_workloads
