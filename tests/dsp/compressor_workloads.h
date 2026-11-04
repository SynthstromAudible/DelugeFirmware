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
/// @brief The compressor's recorded workloads: deterministic runs of blocks, each block's input, knobs and levels as
/// one of the compressor's owners (the song's master, a sound, a clip) drives it, and what a compressor renders from
/// them.
///
/// A run is generic over the compressor class: the firmware's `RmsFeedbackCompressor` or either reference
/// (reference_compressor.h). Each takes the same calls and reports the same `view()`, whose digest is the state pin.
/// The input signals and block lengths are the filters' (filter_workloads.h).

#pragma once

#include "dsp-next/core/types.h"
#include "dsp-next/fixed/dynamics/rms_feedback_compressor.h" // CompressorKnobs
#include "filter_workloads.h"
#include "raw_frame.h"
#include "reference_compressor.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace compressor_workloads {

using deluge::dsp::q31;
using deluge::dsp::fixed::CompressorKnobs;
using deluge::dsp::test::RawFrame;
using filter_workloads::Digest;
using filter_workloads::kMaxFrames;
using filter_workloads::Parting;
using filter_workloads::Signal;
using filter_workloads::WorkloadRecord;

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
	bool min_left = false; ///< Whether the block's middle frame (frames / 2) is replaced: left INT32_MIN, right -2^30.
};

/// @brief Every recorded compressor workload.
///
/// A reset between two sounding renders (the owner's threshold dropping to 0 for a block or two, then rising again) is
/// the ONSET_PARTIAL_WET workloads' blocks 15 to 18: rendered, reset twice without rendering, rendered again, all
/// sounding. The next render smooths its mean against 0 while the last level carries.
enum class CompressorWorkloadId : uint8_t {
	MASTER_SILENCE_THEN_SOUND, ///< On from the start: seeded from a silent frame, then sound arrives.
	MASTER_ONSET_PARTIAL_WET,  ///< Off through silence, on at a sounding first frame; off and on again while sounding.
	SOUND_SILENCE_THEN_SOUND,
	SOUND_ONSET_PARTIAL_WET,
	CLIP_SILENCE_THEN_SOUND,
	CLIP_ONSET_PARTIAL_WET,
	MASTER_VOLUME_STEPS, ///< The master's final volume and channel gains stepped mid-run: the makeup envelope moves.
	BLEND_SWITCH,        ///< A sound's blend alternating between fully wet and about 60% every 3 blocks.
	BLOCK_LENGTHS,       ///< The master at block lengths 1 to 9, then 128, in turn.
	UNEQUAL_VOLUMES,     ///< The master with its channel gains far apart, swapping sides every 8 blocks.
	WRAPPED_SUM,         ///< A clip at 50% blend on full-scale input: a block's sum of squares passes 2^31.
	INT32_MIN_DETECTOR,  ///< A clip all but dry: one frame per block whose high-passed left is exactly INT32_MIN.
	GAIN_PAST_32,        ///< The master at full makeup, its channel gains ±2: the target gains pass ±32.
};

/// @brief How many compressor workloads there are.
inline constexpr size_t kCompressorWorkloadCount = static_cast<size_t>(CompressorWorkloadId::GAIN_PAST_32) + 1;

/// @brief How many workloads the legacy compressor's records cover: those up to MASTER_VOLUME_STEPS.
inline constexpr size_t kLegacyRecordedCount = static_cast<size_t>(CompressorWorkloadId::MASTER_VOLUME_STEPS) + 1;

/// @return Whether a level defect of the legacy compressor is reached in workload @p id.
/// @details The 32-bit sum of squares wraps in WRAPPED_SUM, and in the ONSET_PARTIAL_WET workloads' loud blocks of 24
/// frames and more, whose 40% dry part is near full scale. A detector output is exactly INT32_MIN in
/// INT32_MIN_DETECTOR, where the legacy `std::abs` is undefined.
[[nodiscard]] constexpr bool defects_fire(CompressorWorkloadId id) {
	switch (id) {
	case CompressorWorkloadId::MASTER_ONSET_PARTIAL_WET:
	case CompressorWorkloadId::SOUND_ONSET_PARTIAL_WET:
	case CompressorWorkloadId::CLIP_ONSET_PARTIAL_WET:
	case CompressorWorkloadId::WRAPPED_SUM:
	case CompressorWorkloadId::INT32_MIN_DETECTOR:
		return true;
	default:
		return false;
	}
}

/// @return Whether the legacy compressor's channel gains wrap past 32 in workload @p id: in GAIN_PAST_32 alone.
/// Everywhere neither this nor a level defect fires, the legacy compressor and the fixed one render alike.
[[nodiscard]] constexpr bool gain_wraps(CompressorWorkloadId id) {
	return id == CompressorWorkloadId::GAIN_PAST_32;
}

/// @brief A recorded compressor workload.
struct CompressorWorkload {
	CompressorWorkloadId id;
	std::string_view name;
	Owner owner;
	size_t blocks;
	std::function<CompressorStep(size_t)> step;
};

namespace detail {

using filter_workloads::detail::frames_at;
using filter_workloads::detail::kBlocks;

/// The knobs an owner's workload starts from.
[[nodiscard]] inline CompressorKnobs owner_knobs(Owner owner) {
	switch (owner) {
	case Owner::MASTER:
		return {.attack = deluge::dsp::Q<31>::from_raw(5 << 24),
		        .release = deluge::dsp::Q<31>::from_raw(5 << 24),
		        .threshold = deluge::dsp::Q<31>::from_raw(1 << 30),
		        .ratio = deluge::dsp::Q<31>::from_raw(64 << 24)};
	case Owner::SOUND:
		return {.attack = deluge::dsp::Q<31>::from_raw(3 << 24),
		        .release = deluge::dsp::Q<31>::from_raw(20 << 24),
		        .threshold = deluge::dsp::Q<31>::from_raw(0),
		        .ratio = deluge::dsp::Q<31>::from_raw(100 << 24),
		        .sidechain_hpf = deluge::dsp::Q<31>::from_raw(1 << 30)};
	case Owner::CLIP:
		return {.attack = deluge::dsp::Q<31>::from_raw(40 << 24),
		        .release = deluge::dsp::Q<31>::from_raw(2 << 24),
		        .threshold = deluge::dsp::Q<31>::from_raw(0),
		        .ratio = deluge::dsp::Q<31>::from_raw(20 << 24),
		        .sidechain_hpf = deluge::dsp::Q<31>::from_raw(1 << 28)};
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
			             s.knobs.blend = deluge::dsp::Q<31>::from_raw(kPartialWet);
			             if (b >= 25) {
				             s.knobs.attack = deluge::dsp::Q<31>::from_raw(10 << 24);
				             s.knobs.release = deluge::dsp::Q<31>::from_raw(60 << 24);
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
	w.push_back({.id = CompressorWorkloadId::BLEND_SWITCH,
	             .name = "comp_blend_switch",
	             .owner = Owner::SOUND,
	             .blocks = kBlocks,
	             .step = [](size_t b) {
		             CompressorStep s = owner_step(Owner::SOUND, b);
		             s.knobs.blend = deluge::dsp::Q<31>::from_raw((b / 3) % 2 == 0 ? kOneQ31 : kPartialWet);
		             s.signal = b < 2 ? Signal::SILENCE : Signal::VOICE;
		             return s;
	             }});
	w.push_back({.id = CompressorWorkloadId::BLOCK_LENGTHS,
	             .name = "comp_block_lengths",
	             .owner = Owner::MASTER,
	             .blocks = kBlocks,
	             .step = [](size_t b) {
		             CompressorStep s = owner_step(Owner::MASTER, b);
		             s.frames = b % 10 < 9 ? (b % 10) + 1 : kMaxFrames;
		             s.signal = b % 7 == 6 ? Signal::LOUD : Signal::VOICE;
		             return s;
	             }});
	w.push_back({.id = CompressorWorkloadId::UNEQUAL_VOLUMES,
	             .name = "comp_unequal_volumes",
	             .owner = Owner::MASTER,
	             .blocks = kBlocks,
	             .step = [](size_t b) {
		             CompressorStep s = owner_step(Owner::MASTER, b);
		             s.signal = b % 7 == 6 ? Signal::LOUD : Signal::VOICE;
		             const bool left_loud = (b / 8) % 2 == 0;
		             s.volume_l = left_loud ? 3 << 26 : 1 << 24;
		             s.volume_r = left_loud ? 1 << 24 : 3 << 26;
		             return s;
	             }});
	w.push_back({.id = CompressorWorkloadId::WRAPPED_SUM,
	             .name = "comp_wrapped_sum",
	             .owner = Owner::CLIP,
	             .blocks = kBlocks,
	             .step = [](size_t b) {
		             CompressorStep s = owner_step(Owner::CLIP, b);
		             s.knobs.blend = deluge::dsp::Q<31>::from_raw(1 << 30);
		             s.signal = Signal::FULL_SCALE;
		             return s;
	             }});
	w.push_back({.id = CompressorWorkloadId::INT32_MIN_DETECTOR,
	             .name = "comp_int32_min_detector",
	             .owner = Owner::CLIP,
	             .blocks = kBlocks,
	             .step = [](size_t b) {
		             CompressorStep s = owner_step(Owner::CLIP, b);
		             // Wet at 2^-31: a frame of INT32_MIN comes out as -2^31 when its saturated wet part, scaled by the
		             // blend, rounds to -1. With the detector's high-pass at 0 its low-pass stays at 0, so the detector
		             // passes the frame as it is. The other frames are quiet, so no block's sum wraps.
		             s.knobs.blend = deluge::dsp::Q<31>::from_raw(1);
		             s.knobs.sidechain_hpf = deluge::dsp::Q<31>::from_raw(0);
		             s.min_left = true;
		             return s;
	             }});
	w.push_back({.id = CompressorWorkloadId::GAIN_PAST_32,
	             .name = "comp_gain_past_32",
	             .owner = Owner::MASTER,
	             .blocks = kBlocks,
	             .step = [](size_t b) {
		             CompressorStep s = owner_step(Owner::MASTER, b);
		             // The threshold knob at its top and the final volume at full scale drive the makeup, and so the
		             // gain, to its cap of 31; at a channel gain of ±2 the targets ask for ±62.
		             s.knobs.threshold = deluge::dsp::Q<31>::from_raw(INT32_MAX);
		             s.final_volume = INT32_MAX;
		             s.volume_l = 1 << 28;
		             s.volume_r = -(1 << 28);
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
[[nodiscard]] inline std::vector<RawFrame> compressor_input(const CompressorWorkload& workload, size_t b) {
	const CompressorStep s = workload.step(b);
	std::vector<RawFrame> block(s.frames);
	for (size_t i = 0; i < block.size(); ++i) {
		const auto k = static_cast<uint32_t>((b * 1024) + (2 * i));
		block[i] = {.l = filter_workloads::detail::signal_sample(s.signal, k),
		            .r = filter_workloads::detail::signal_sample(s.signal, k + 1)};
	}
	if (s.min_left) {
		block[block.size() / 2] = {.l = INT32_MIN, .r = -(1 << 30)};
	}
	return block;
}

/// @brief One run of a compressor workload through compressor @p C, as its owner drives it.
template <typename C>
class CompressorRun {
public:
	explicit CompressorRun(const CompressorWorkload& workload) : workload_{workload} {
		if (workload_.owner == Owner::MASTER) {
			compressor_.set_base_gain(0.85F);
		}
	}

	/// @brief Renders block @p b into @p out (resized to the block); a block the owner does not render is left as
	/// its input.
	/// @return The gain reduction the compressor reports after the block.
	uint8_t block(size_t b, std::vector<RawFrame>& out) {
		const CompressorStep s = workload_.step(b);
		out = compressor_input(workload_, b);
		compressor_.configure(s.knobs);
		if (workload_.owner != Owner::MASTER) {
			if constexpr (kTyped) {
				compressor_.set_threshold(deluge::dsp::Q<31>::from_raw(s.threshold));
			}
			else {
				compressor_.set_threshold(s.threshold);
			}
		}
		if (!s.render) {
			compressor_.reset();
			return compressor_.gain_reduction();
		}
		const std::span<RawFrame> block{out};
		if constexpr (kTakesScratch) {
			const std::span<RawFrame> scratch{scratch_.data(), out.size()};
			if (workload_.owner == Owner::MASTER) {
				compressor_.render(block, s.volume_l, s.volume_r, s.final_volume, scratch);
			}
			else {
				compressor_.render_volume_neutral(block, s.final_volume, scratch);
			}
		}
		else if constexpr (kTyped) {
			// The levels as the firmware's compressor takes them: the gains Q27, the final volume Q29
			if (workload_.owner == Owner::MASTER) {
				compressor_.render(deluge::dsp::test::typed_frames(block), deluge::dsp::Q<27>::from_raw(s.volume_l),
				                   deluge::dsp::Q<27>::from_raw(s.volume_r),
				                   deluge::dsp::Q<29>::from_raw(s.final_volume));
			}
			else {
				compressor_.render_volume_neutral(deluge::dsp::test::typed_frames(block),
				                                  deluge::dsp::Q<29>::from_raw(s.final_volume));
			}
		}
		else {
			if (workload_.owner == Owner::MASTER) {
				compressor_.render(block, s.volume_l, s.volume_r, s.final_volume);
			}
			else {
				compressor_.render_volume_neutral(block, s.final_volume);
			}
		}
		return compressor_.gain_reduction();
	}

	/// @return The compressor, between blocks.
	[[nodiscard]] const C& compressor() const { return compressor_; }

	/// @return The digest of the compressor's state.
	[[nodiscard]] uint64_t state() const { return compressor_reference::digest(compressor_.view()); }

private:
	/// Whether @p C blends from a caller's scratch, as the references do; the firmware's compressor blends in place.
	static constexpr bool kTakesScratch =
	    requires(C& c, std::span<RawFrame> b) { c.render(b, q31{}, q31{}, q31{}, b); };
	/// Whether @p C takes its threshold and levels typed, as the firmware's compressor does; the references take raw
	/// words.
	static constexpr bool kTyped = requires(C& c) { c.set_threshold(deluge::dsp::Q<31>{}); };

	const CompressorWorkload& workload_;
	C compressor_{};
	std::array<RawFrame, kMaxFrames> scratch_{};
};

/// @return Where @p workload, played through compressor @p A and through compressor @p B, first parts.
template <typename A, typename B>
[[nodiscard]] std::optional<Parting> first_parting(const CompressorWorkload& workload) {
	CompressorRun<A> a{workload};
	CompressorRun<B> b{workload};
	std::vector<RawFrame> out_a;
	std::vector<RawFrame> out_b;
	for (size_t block = 0; block < workload.blocks; ++block) {
		const uint8_t ra = a.block(block, out_a);
		const uint8_t rb = b.block(block, out_b);
		if (!std::ranges::equal(out_a, out_b, [](RawFrame x, RawFrame y) { return x.l == y.l && x.r == y.r; })) {
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

/// @return @p workload's record, rendered by compressor @p C: each block's output digest (its samples, then the gain
/// reduction) and the state at the end. Nothing draws noise.
template <typename C>
[[nodiscard]] WorkloadRecord record(const CompressorWorkload& workload) {
	CompressorRun<C> run{workload};
	WorkloadRecord out;
	std::vector<RawFrame> block;
	for (size_t b = 0; b < workload.blocks; ++b) {
		const uint8_t reduction = run.block(b, block);
		Digest output;
		output.add(std::span<const RawFrame>{block});
		output.add(reduction);
		out.outputs.push_back(output.value);
	}
	out.state = run.state();
	return out;
}

} // namespace compressor_workloads
