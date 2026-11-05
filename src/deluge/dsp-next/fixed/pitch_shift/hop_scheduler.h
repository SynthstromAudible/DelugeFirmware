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

#include "definitions_cxx.hpp" // kMaxSampleValue, kInterpolationMaxNumSamples
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/fixed/noise/cong.h"
#include "dsp-next/fixed/pitch_shift/hop_planner.h"
#include "dsp-next/fixed/pitch_shift/input_history.h"
#include "dsp-next/fixed/pitch_shift/play_head.h"

#include <algorithm>
#include <cstdint>
#include <optional>

/// @file
/// @brief When the live pitch shifter hops, and how its heads cross-fade between hops.

namespace deluge::dsp::fixed::pitch_shift {

/// @brief The shifter's two heads: the newer one, and the older one fading out after a hop.
template <typename Frame>
struct PlayHeads {
	using Kernel = PlayHead<Frame>;
	Mono<Kernel> newer;
	Mono<Kernel> older;
};

/// @brief One stretch of a block between hop decisions, and each head's amplitude across it.
struct Segment {
	int32_t length = 0; ///< Output samples.
	Q<31> newer{};      ///< The newer head's amplitude before the stretch's first step.
	Q<31> newer_step{}; ///< Its step per sample.
	Q<31> older{};      ///< The older head's, while it fades out.
	Q<31> older_step{};
	bool older_sounding = false; ///< Whether the older head is still fading out.
};

/// @brief Decides when the live pitch shifter hops, cuts each block at its hops, and cross-fades the heads.
/// @details Each block is iterated: `begin_block`, then `while (auto s = scheduler.next(...))`, each segment rendered
/// by the owner's heads. Before each segment, pitching up, a hop the newer head cannot last is shortened, and the
/// crossfade is hurried if the older head will run out first; then, while no crossfade runs and the newer head is
/// repitching, a hop is cut short where the input is more percussive now than at the head by the tuning's threshold.
/// A hop that has ended is planned (@ref HopPlanner) and starts a linear crossfade. After a block's first hop, its
/// later segments skip both checks (quirk Q13, docs/dev/live_pitch_shifter_quirks.md).
template <typename Frame>
class HopScheduler {
public:
	using Kernel = PlayHead<Frame>;

	/// @brief What the owner hands each block.
	struct Block {
		int32_t length = 0;                ///< Output samples.
		int32_t phase_increment = 1 << 24; ///< The shift, 8.24.
		Q<31> amplitude{};                 ///< The voice's source amplitude at the block's start.
		Q<31> amplitude_step{};            ///< Its step per sample.
	};

	/// @brief A scheduler starting at @p phase_increment: its first hop and crossfade, by the shift.
	explicit HopScheduler(int32_t phase_increment) {
		if (phase_increment < kMaxSampleValue) {
			progress_.next_crossfade_length = progress_.samples_til_hop_end = kInterpolationMaxNumSamples * 2;
		}
		else if (phase_increment < kUpOneSemitone) {
			progress_.samples_til_hop_end = kFirstSmallUpHop;
			progress_.next_crossfade_length = kFirstSmallUpCrossfade;
		}
		else {
			progress_.next_crossfade_length = progress_.samples_til_hop_end = kFirstHop;
		}
	}

	/// @brief Starts a block over @p history, whose latest frames are the block's.
	void begin_block(const Block& block, const InputHistory<Frame>& history) {
		block_ = block;
		remaining_ = block.length;
		amplitude_ = block.amplitude.raw(); // The ramp's word, stepped as the crossfade computes
		// The frames written at the block's start, as a 32-bit count widened
		start_frames_ = history.frames_written() - static_cast<uint32_t>(block.length);
		hopped_ = false;
		hops_ = 0;
	}

	/// @brief The block's next segment, or none once the block is covered.
	[[nodiscard]] std::optional<Segment> next(const InputHistory<Frame>& history, PlayHeads<Frame>& heads,
	                                          Cong& noise) {
		if (remaining_ == 0) {
			return std::nullopt;
		}
		int32_t length = 0;
		bool forced = false;
		while (true) {
			if (!hopped_ && block_.phase_increment > kMaxSampleValue) {
				fit_hop_to_heads(history, heads);
			}
			if (!hopped_ && !crossfading() && progress_.samples_til_hop_end != 0
			    && heads.newer.state().kind != HeadKind::DIRECT) {
				check_onset(history, heads);
			}
			if (progress_.samples_til_hop_end != 0) {
				length = std::min(remaining_, progress_.samples_til_hop_end);
				// No head reads past the latest input: the newer head's hop ends where it would (hopping now if it has
				// no reach left, once), and the older head's crossfade is hurried to end where it would
				const int32_t newer = reach(heads.newer, history);
				if (newer == 0 && !forced) {
					progress_.samples_til_hop_end = 0;
					forced = true;
				}
				else {
					if (newer < length) {
						length = std::max(newer, int32_t{1});
						progress_.samples_til_hop_end = length;
					}
					break;
				}
			}
			if (progress_.samples_til_hop_end == 0) {
				hop(history, heads, noise);
				++hops_;
				hopped_ = true;
			}
		}
		if (crossfading()) {
			const int32_t older = reach(heads.older, history);
			if (older < length) {
				length = std::max(older, int32_t{1});
				hurry_crossfade(static_cast<uint32_t>(length));
			}
		}
		const Segment segment = crossfade(length);
		progress_.samples_til_hop_end -= segment.length;
		progress_.samples_into_hop += segment.length;
		remaining_ -= segment.length;
		amplitude_ = static_cast<int32_t>(
		    static_cast<uint32_t>(amplitude_)
		    + (static_cast<uint32_t>(block_.amplitude_step.raw()) * static_cast<uint32_t>(segment.length)));
		start_frames_ += segment.length;
		return segment;
	}

	/// @brief Where the hop and its crossfade are.
	struct Progress {
		uint32_t crossfade_progress = kMaxSampleValue; ///< Q24: the newer head's share; at 1 no crossfade runs.
		uint32_t crossfade_increment = 0;              ///< The crossfade's step per sample, Q24.
		int32_t next_crossfade_length = 0;             ///< The next hop's crossfade, in output samples.
		int32_t samples_til_hop_end = 0;               ///< Output samples until the hop ends.
		int32_t samples_into_hop = 0;                  ///< Output samples the hop has run.
		int32_t percussive_threshold = 0;              ///< The onset threshold the hop's tuning set.
	};

	/// @brief Where the hop and its crossfade are.
	[[nodiscard]] const Progress& progress() const { return progress_; }

	/// @brief The hops this block made.
	[[nodiscard]] int32_t hops() const { return hops_; }
	/// @brief Whether the older head is still fading out.
	[[nodiscard]] bool crossfading() const { return progress_.crossfade_progress < kMaxSampleValue; }

private:
	/// One semitone up, 8.24: below it the first hop is long, 2048 samples, with a 256-sample crossfade.
	static constexpr int32_t kUpOneSemitone = 17774841;
	static constexpr int32_t kFirstSmallUpHop = 2048;
	static constexpr int32_t kFirstSmallUpCrossfade = 256;
	/// The first hop and crossfade further up: 256 samples each.
	static constexpr int32_t kFirstHop = 256;
	/// A playtime too long to run out.
	static constexpr int32_t kForever = 2147483647;
	/// How far ahead a playtime is counted: past the longest hop the tuning allows (140 hundred samples).
	static constexpr int64_t kPlaytimeHorizon = 16384;

	/// @brief The output samples @p head can play before it runs out of input, pitching up.
	/// @details The input arrives a block at a time: this block's is all written, and each later block brings a block
	/// more. The head reads its step per sample, so it lasts the rest of this block if its reach does, then each block
	/// in which the input it has covers a block's reading, and runs out in the first that does not. Counted to the
	/// longest hop the tuning allows; a direct head never runs out.
	[[nodiscard]] int32_t playtime(const Mono<Kernel>& head, const InputHistory<Frame>& history) const {
		if (head.state().kind != HeadKind::REPITCHING) {
			return kForever;
		}
		const auto step = static_cast<int64_t>(static_cast<uint32_t>(block_.phase_increment));
		const auto block = static_cast<int64_t>(block_.length);
		// The input ahead of the head, Q24 frames, less where its phase already stands
		int64_t ahead = (static_cast<int64_t>(history.now().since(head.state().cursor)) << 24)
		                - static_cast<int64_t>(head.state().phase);
		const auto rest = static_cast<int64_t>(remaining_);
		if (ahead < rest * step) {
			return static_cast<int32_t>(std::max<int64_t>(ahead, 0) / step);
		}
		ahead -= rest * step;
		int64_t played = rest;
		while (played < kPlaytimeHorizon) {
			ahead += block << 24;
			if (ahead < block * step) {
				return static_cast<int32_t>(played + (ahead / step));
			}
			ahead -= block * step;
			played += block;
		}
		return kForever;
	}

	/// @brief The samples @p head can render before it reads the latest frame written, at the block's shift: with phase
	/// φ and step r, k steps read up to the frame φ + k·r reaches, so k = (distance · 2^24 − φ) / r. A direct head
	/// reads one frame per sample and is placed behind the block's input, so it never runs out within a block.
	[[nodiscard]] int32_t reach(const Mono<Kernel>& head, const InputHistory<Frame>& history) const {
		if (head.state().kind != HeadKind::REPITCHING) {
			return kForever;
		}
		const auto ahead = (static_cast<int64_t>(history.now().since(head.state().cursor)) << 24)
		                   - static_cast<int64_t>(head.state().phase);
		if (ahead <= 0) {
			return 0;
		}
		const int64_t steps = ahead / static_cast<int64_t>(static_cast<uint32_t>(block_.phase_increment));
		return steps >= kForever ? kForever : static_cast<int32_t>(steps);
	}

	/// @brief Pitching up: shortens a hop the newer head cannot last, and hurries the crossfade if the older head will
	/// run out first.
	void fit_hop_to_heads(const InputHistory<Frame>& history, const PlayHeads<Frame>& heads) {
		const int32_t newer = playtime(heads.newer, history);
		if (progress_.samples_til_hop_end + progress_.next_crossfade_length > newer) {
			progress_.next_crossfade_length =
			    std::min(progress_.next_crossfade_length, (newer + progress_.samples_into_hop) >> 1);
			progress_.samples_til_hop_end = newer - progress_.next_crossfade_length;
			if (progress_.samples_til_hop_end < 0) {
				progress_.samples_til_hop_end = 0;
				progress_.next_crossfade_length = std::max(newer, int32_t{0});
				progress_.crossfade_progress = kMaxSampleValue;
			}
			else if (progress_.samples_til_hop_end > 0 && crossfading()) {
				hurry_crossfade(progress_.samples_til_hop_end);
			}
		}
		if (progress_.samples_til_hop_end != 0 && crossfading()) {
			const auto older = static_cast<uint32_t>(playtime(heads.older, history));
			if (older == 0) {
				progress_.crossfade_increment = kMaxSampleValue;
			}
			else {
				hurry_crossfade(older);
			}
		}
	}

	/// @brief Raises the crossfade's step so it ends within @p samples.
	void hurry_crossfade(uint32_t samples) {
		const uint32_t least = static_cast<uint32_t>(kMaxSampleValue - progress_.crossfade_progress) / samples + 1;
		progress_.crossfade_increment = std::max(progress_.crossfade_increment, least);
	}

	/// @brief Cuts the hop short where the input is more percussive now than at the newer head, by the threshold.
	/// @details Checked once per percussiveness entry, at the head or at now: an entry already checked at both is
	/// not checked again.
	void check_onset(const InputHistory<Frame>& history, PlayHeads<Frame>& heads) {
		const uint32_t latest = history.frames_written();
		const uint32_t behind = history.now().since(heads.newer.state().cursor);
		const uint32_t at_head = (latest - behind - 1) >> kPercussivenessStepMagnitude;
		const auto now_before = static_cast<uint32_t>((start_frames_ - 1) >> kPercussivenessStepMagnitude);
		const uint32_t now_entry = (latest - 1) >> kPercussivenessStepMagnitude;
		if (now_entry != at_head && (at_head != heads.newer.state().percussiveness_entry || now_entry != now_before)) {
			if (history.percussiveness_at(now_entry)
			    >= history.percussiveness_at(at_head) + progress_.percussive_threshold) {
				progress_.samples_til_hop_end = 0;
			}
		}
		heads.newer.template on<&Kernel::note_percussiveness_entry>(at_head);
	}

	/// @brief Ends the hop: the newer head becomes the older, and the new newer head starts where the planner puts it.
	void hop(const InputHistory<Frame>& history, PlayHeads<Frame>& heads, Cong& noise) {
		heads.older.template on<&Kernel::take>(heads.newer.state());
		const auto this_crossfade = static_cast<uint32_t>(progress_.next_crossfade_length);
		const HopPlan plan =
		    HopPlanner::plan(block_.phase_increment, history, heads.older.state(), this_crossfade,
		                     progress_.samples_into_hop, start_frames_, history.frames_written(), noise);
		progress_.percussive_threshold = plan.tuning.percussive_threshold;
		progress_.next_crossfade_length = plan.tuning.crossfade_length;
		progress_.samples_til_hop_end = plan.hop_length;
		heads.newer.template on<&Kernel::begin_hop>(
		    typename Kernel::Hop{.cursor = plan.cursor, .phase = plan.phase, .kind = plan.kind});
		if (this_crossfade != 0) {
			progress_.crossfade_progress = 0;
			progress_.crossfade_increment = (kMaxSampleValue - 1) / this_crossfade + 1;
		}
		else {
			progress_.crossfade_progress = kMaxSampleValue;
		}
		progress_.samples_into_hop = 0;
	}

	/// @brief The heads' amplitudes across the next @p length samples, the crossfade advancing over them.
	/// @details A linear crossfade: its Q24 progress is the newer head's share of the voice's amplitude, the older's
	/// the rest; the shares' step across the segment is added to and taken from the voice's own.
	[[nodiscard]] Segment crossfade(int32_t length) {
		Segment s{.length = length, .older_sounding = crossfading()};
		if (!s.older_sounding) {
			s.newer = Q<31>::from_raw(amplitude_);
			s.newer_step = block_.amplitude_step;
			return s;
		}
		// The crossfade's Q24 progress as the newer head's Q31 share, the older's the rest of full scale
		const int32_t newer_share =
		    as_wrap<Q<31>>(Q<24>::from_raw(static_cast<int32_t>(progress_.crossfade_progress))).raw();
		const int32_t older_share = kMaxQ31 - newer_share;
		progress_.crossfade_progress += progress_.crossfade_increment * static_cast<uint32_t>(length);
		// Where the progress will be, held to [−1, 1) before its move to Q31
		const int32_t newer_share_after =
		    as_wrap<Q<31>>(clamp_bits<25>(Q<24>::from_raw(static_cast<int32_t>(progress_.crossfade_progress)))).raw();
		// The difference wraps: a hurried crossfade can carry the progress past full scale within one segment
		const int32_t share_step =
		    sub_wrap(Q<31>::from_raw(newer_share_after), Q<31>::from_raw(newer_share)).raw() / length;
		// The source amplitude times each share (and its step), moved back to Q31
		const auto source = Q<31>::from_raw(amplitude_);
		const int32_t change = mul_as_wrap<Q<31>>(source, Q<31>::from_raw(share_step)).raw();
		s.newer = mul_as_wrap<Q<31>>(source, Q<31>::from_raw(newer_share));
		s.newer_step = add_wrap(block_.amplitude_step, Q<31>::from_raw(change));
		s.older = mul_as_wrap<Q<31>>(source, Q<31>::from_raw(older_share));
		s.older_step = sub_wrap(block_.amplitude_step, Q<31>::from_raw(change));
		return s;
	}

	static constexpr int32_t kMaxQ31 = 2147483647;

	Block block_{};
	int32_t remaining_ = 0;
	int32_t amplitude_ = 0;     ///< The voice's amplitude at the next segment's start, as a word.
	uint64_t start_frames_ = 0; ///< The frames written at the next segment's start, as the hop counts them.
	bool hopped_ = false;
	int32_t hops_ = 0;

	Progress progress_{};
};

} // namespace deluge::dsp::fixed::pitch_shift
