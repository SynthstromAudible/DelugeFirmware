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

#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/dynamics/rms_feedback_compressor.h" // CompressorKnobs
#include "filter_workloads.h"
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
using deluge::dsp::StereoSample;
using deluge::dsp::fixed::CompressorKnobs;
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

using filter_workloads::detail::frames_at;
using filter_workloads::detail::kBlocks;

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
		block[i] = {.l = filter_workloads::detail::signal_sample(s.signal, k),
		            .r = filter_workloads::detail::signal_sample(s.signal, k + 1)};
	}
	return block;
}

/// @brief One run of a compressor workload through compressor @p C, as its owner drives it.
template <typename C>
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

	/// @return The compressor, between blocks.
	[[nodiscard]] const C& compressor() const { return compressor_; }

	/// @return The digest of the compressor's state.
	[[nodiscard]] uint64_t state() const { return compressor_reference::digest(compressor_.view()); }

private:
	const CompressorWorkload& workload_;
	C compressor_{};
	std::array<StereoSample, kMaxFrames> scratch_{};
};

/// @return Where @p workload, played through compressor @p A and through compressor @p B, first parts.
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

/// @return @p workload's record, rendered by compressor @p C: each block's output digest (its samples, then the gain
/// reduction) and the state at the end. Nothing draws noise.
template <typename C>
[[nodiscard]] WorkloadRecord record(const CompressorWorkload& workload) {
	CompressorRun<C> run{workload};
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

} // namespace compressor_workloads
