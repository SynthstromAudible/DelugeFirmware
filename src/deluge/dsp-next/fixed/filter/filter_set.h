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

#pragma once

#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/filter/fade_in.h"
#include "dsp-next/fixed/filter/filter_modes.h"
#include "dsp-next/fixed/filter/high_pass_ladder.h"
#include "dsp-next/fixed/filter/low_pass_ladder.h"
#include "dsp-next/fixed/filter/svf.h"
#include "dsp-next/fixed/noise/cong.h"

#include <span>

namespace deluge::dsp::fixed {

/// @brief The scratch a filter needs while it fades between dry and wet: the caller's, at least the block's samples
/// (two per frame in stereo). A block neither filter fades does not touch it, so it may be shorter, or empty, then.
using FadeScratch = std::span<q31>;

/// @brief The scratch a set needs, each at least the block's samples (two per frame in stereo): the fades' (only on
/// a block a filter fades, see @ref FadeScratch), and the parallel route's second copy of the block.
struct FilterScratch {
	/// The dry copy a filter blends with while it fades in; the two filters use it one after the other.
	FadeScratch fade;
	/// The parallel route's copy of the block, which the high-pass runs over before it is added back.
	std::span<q31> parallel;
};

/// @brief A low-pass and a high-pass filter in series either way or in parallel.
///
/// The low-pass resets when its family (ladder or SVF) changes and the high-pass when its mode changes at all; either
/// fades in from dry when switched on from off. A fade under way carries on across a change of family. Each slot holds
/// one filter at a time, the one its family needs.
///
/// **Components.** Each slot's filter is a kernel in its `Stereo` wrapper (@ref LowPassLadder over the synth's noise
/// stream, @ref HighPassLadder, @ref Svf). A stereo block runs the wrapper over its frames; a mono block runs its left
/// channel alone (`renderLane<0>(…)`), so a voice that moves between mono and stereo blocks keeps one state per
/// channel, the left carried across, as a filter holding both channels does.
///
/// **The fades.** Each slot's fade is a @ref FadeInMono component the set holds beside the slot's union, not inside
/// it: a family change rebuilds the union member and leaves the fade where it is, so a fade under way carries on
/// with no copy. It runs as its own pass, after the filter, only on blocks it is active for, over the block's
/// samples: mono, or interleaved stereo stepping once per channel-sample, left then right.
class FilterSet {
public:
	/// @brief A set in its reset state (see @ref reset): both filters off, nothing fading.
	FilterSet() { reset(); }

	/// @brief Takes one block's settings.
	/// @param gain The volume going into the set, Q31. The low-pass compensates it, the set scales it, and the
	/// high-pass compensates the result.
	/// @param low The low-pass's settings.
	/// @param low_slot What the low-pass slot runs: a ladder mode, an SVF mode, or OFF. A change of family resets the
	/// slot's filter; a change from OFF fades it in from dry.
	/// @param high The high-pass's settings.
	/// @param high_slot What the high-pass slot runs: the ladder, an SVF mode, or OFF. Any change of mode resets the
	/// slot's filter; a change from OFF fades it in from dry.
	/// @param route The order the two filters run in, or PARALLEL to run each over its own copy of the block and sum
	/// the two.
	/// @return The gain going out of the set (Q31): compensated for each filter that is on, and scaled so that at 50%
	/// resonance the level is unchanged.
	[[nodiscard]] q31 configure(q31 gain, const FilterConfig& low, LowPassSlot low_slot, const FilterConfig& high,
	                            HighPassSlot high_slot, FilterRoute route);

	/// @brief Filters a mono @p block in place.
	/// @param noise The noise stream the low-pass ladder draws from, written back after the block.
	void renderMono(std::span<q31> block, FilterScratch scratch, Cong& noise);

	/// @brief Filters an interleaved L/R block in place.
	/// @param noise The noise stream the low-pass ladder draws from, left then right in every frame, written back
	/// after the block.
	void renderStereo(std::span<q31> interleaved, FilterScratch scratch, Cong& noise);

	/// @brief Back to a new set's state: both filters off and every filter's state zero, the fades included (so the
	/// next configure fades nothing in). Both slots read as holding a ladder, so neither the low-pass ladder nor the
	/// high-pass ladder is reset by the first configure after this (their state is zero already); an SVF in either slot
	/// is reset, without a fade.
	void reset();

	/// @brief Whether either filter is on.
	[[nodiscard]] bool isOn() const { return isLowPassOn() || isHighPassOn(); }
	/// @brief Whether the low-pass is on.
	[[nodiscard]] bool isLowPassOn() const { return low_slot_.kind != LowPassSlot::Kind::OFF; }
	/// @brief Whether the high-pass is on.
	[[nodiscard]] bool isHighPassOn() const { return high_slot_.kind != HighPassSlot::Kind::OFF; }

	using LowPassLadderFilter = Stereo<LowPassLadder<>>; ///< The low-pass ladder, both channels.
	using HighPassLadderFilter = Stereo<HighPassLadder>; ///< The high-pass ladder, both channels.
	using SvfFilter = Stereo<Svf>;                       ///< The SVF, both channels, in either slot.

	/// @brief The set between blocks, read-only: what each slot holds, how it was last configured, and its fade.
	/// @details Exactly one of each slot's two filter pointers is set, the one its held kind names; the other is
	/// nullptr. The pointers stay valid until the next configure or reset.
	struct View {
		LowPassSlot::Kind low_held;              ///< The family the low-pass slot holds (LADDER or SVF).
		const LowPassLadderFilter* low_ladder;   ///< The low-pass ladder, when held.
		const SvfFilter* low_svf;                ///< The low-pass SVF, when held.
		HighPassSlot::Kind high_held;            ///< The family the high-pass slot holds (LADDER or SVF).
		const HighPassLadderFilter* high_ladder; ///< The high-pass ladder, when held.
		const SvfFilter* high_svf;               ///< The high-pass SVF, when held.
		LowPassSlot low_slot;                    ///< The low-pass as configured (OFF when off).
		HighPassSlot high_slot;                  ///< The high-pass as configured (OFF when off).
		LowPassSlot::Kind low_last;              ///< The low-pass's family as last configured.
		HighPassSlot high_last;                  ///< The high-pass's mode as last configured.
		FilterRoute route;                       ///< The route as configured.
		FadeIn::State low_fade;                  ///< The low-pass slot's fade levels.
		FadeIn::State high_fade;                 ///< The high-pass slot's fade levels.
	};

	/// @brief The set between blocks, read-only (see @ref View).
	[[nodiscard]] View view() const;

private:
	using Fade = Mono<FadeInMono>; ///< A slot's fade, over mono or interleaved blocks.

	/// The low-pass slot's filter: the ladder or the SVF, whichever @ref low_held_ names.
	union LowPass {
		LowPassLadderFilter ladder;
		SvfFilter svf;
		LowPass() : ladder{} {}
	};
	/// The high-pass slot's filter: the ladder or the SVF, whichever @ref high_held_ names.
	union HighPass {
		HighPassLadderFilter ladder;
		SvfFilter svf;
		HighPass() : ladder{} {}
	};

	/// @brief Makes the low-pass slot hold @p kind's filter (LADDER or SVF): a new one, its state zero. The slot's
	/// fade carries on.
	void holdLowPass(LowPassSlot::Kind kind);
	/// @brief Makes the high-pass slot hold @p kind's filter (LADDER or SVF): a new one, its state zero. The slot's
	/// fade carries on.
	void holdHighPass(HighPassSlot::Kind kind);

	/// @brief The low-pass over a mono block, if on.
	void renderLowPassMono(std::span<q31> block, FadeScratch scratch, Cong& noise);
	/// @brief The high-pass over a mono block, if on.
	void renderHighPassMono(std::span<q31> block, FadeScratch scratch);
	/// @brief The low-pass over an interleaved block, if on.
	void renderLowPassStereo(std::span<q31> interleaved, FadeScratch scratch, Cong& noise);
	/// @brief The high-pass over an interleaved block, if on.
	void renderHighPassStereo(std::span<q31> interleaved, FadeScratch scratch);

	LowPass low_pass_;
	HighPass high_pass_;
	/// The low-pass slot's fade, whichever filter the slot holds.
	Fade low_fade_;
	/// The high-pass slot's fade, whichever filter the slot holds.
	Fade high_fade_;

	/// The family @ref low_pass_ holds (LADDER or SVF).
	LowPassSlot::Kind low_held_{LowPassSlot::Kind::LADDER};
	/// The family @ref high_pass_ holds (LADDER or SVF).
	HighPassSlot::Kind high_held_{HighPassSlot::Kind::LADDER};
	/// The low-pass as configured (OFF when off), which the render runs.
	LowPassSlot low_slot_{};
	/// The high-pass as configured (OFF when off), which the render runs.
	HighPassSlot high_slot_{};
	/// The low-pass's family as last configured, OFF when off; after a reset, LADDER (what the zeroed slot holds).
	LowPassSlot::Kind low_last_{LowPassSlot::Kind::LADDER};
	/// The high-pass's mode as last configured, OFF when off; after a reset, the ladder (what the zeroed slot holds).
	HighPassSlot high_last_{.kind = HighPassSlot::Kind::LADDER};
	/// The route as configured.
	FilterRoute route_{FilterRoute::HIGH_TO_LOW};
};

} // namespace deluge::dsp::fixed
