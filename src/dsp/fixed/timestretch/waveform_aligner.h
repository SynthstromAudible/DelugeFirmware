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

#include "dsp/core/fixed_point.h"
#include "dsp/fixed/sample_read/frame_source.h"
#include "dsp/fixed/timestretch/waveform_search.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <tuple>

namespace deluge::dsp::fixed::timestretch {

/// @file
/// Frames are read as words and the phases are Q24 words added with a whole-frame carry, as the legacy computed them,
/// so the typed increment and phases cross to words where A1, A3 and A5 compute.
/// @brief The time stretcher's waveform aligner: where its new head starts so its waveform best matches the old
/// head's through the crossfade (A1 to A6 of docs/dev/time_stretcher_contracts.md), over the search it shares with the
/// pitch shifter.

using sample_read::Direction;
using sample_read::Increment;

/// The frames each moving average spans at the native rate.
inline constexpr int32_t kAverageFrames = 35;

/// @brief How the search measures: each average's frames, and the crossfade in source frames (A1).
struct AlignSetup {
	int32_t each;
	int32_t crossfade_source_frames;
};

/// @brief The search's setup at @p increment for a crossfade of @p crossfade_frames output frames (A1): each average
/// the native span repitched, held to 1 to twice the span.
[[nodiscard]] constexpr AlignSetup align_setup(Increment increment, int32_t crossfade_frames) {
	const auto each = static_cast<int32_t>((uint64_t{increment.raw()} * kAverageFrames) >> 24);
	return {.each = std::clamp(each, 1, kAverageFrames * 2),
	        .crossfade_source_frames =
	            static_cast<int32_t>((uint64_t{static_cast<uint32_t>(crossfade_frames)} * increment.raw()) >> 24)};
}

/// @brief The moving averages' first frame for a head at @p start_frame (A2): the crossfade's middle, less half the
/// three averages.
[[nodiscard]] constexpr int32_t averages_read(int32_t start_frame, int32_t crossfade_source_frames, Direction direction,
                                              int32_t each) {
	const auto d = static_cast<int32_t>(direction);
	const int32_t middle = start_frame + (crossfade_source_frames >> 1) * d;
	return middle - ((each * static_cast<int32_t>(std::tuple_size_v<Averages>)) >> 1) * d;
}

/// @brief A frame's channels' high words, summed, read from @p source through the run it last gave: a frame outside
/// that run asks for another, forwards from the frame or, below the run, reversed, so a search asks once per chunk.
template <sample_read::frame_source S>
class FoldedFrames {
public:
	explicit FoldedFrames(const S& source) : source_(&source) {}

	/// nullopt where the frame is not resident.
	[[nodiscard]] std::optional<int32_t> operator()(int32_t frame) const {
		if (frame < lo_ || frame > hi_) {
			const bool below = run_.has_value() && frame < lo_;
			const Direction d = below ? Direction::REVERSE : Direction::FORWARD;
			run_ = source_->run(frame, d);
			if (!run_) {
				lo_ = 1;
				hi_ = 0;
				return std::nullopt;
			}
			base_ = frame;
			reversed_ = below;
			lo_ = below ? frame - run_->frames + 1 : frame;
			hi_ = below ? frame : frame + run_->frames - 1;
		}
		const int32_t k = reversed_ ? base_ - frame : frame - base_;
		int32_t value = sample_read::frame_word(*run_, k, 0) >> 16;
		if (run_->channels == 2) {
			value += sample_read::frame_word(*run_, k, 1) >> 16;
		}
		return value;
	}

private:
	const S* source_;
	mutable std::optional<sample_read::FrameRun> run_;
	mutable int32_t base_ = 0;
	mutable int32_t lo_ = 1;
	mutable int32_t hi_ = 0;
	mutable bool reversed_ = false;
};

/// @brief The three moving averages of a head starting at @p start_frame (A2): each @p each frames of high words in
/// @p direction from averages_read(). Forwards only, refused within half the crossfade of either end (A2's quirk:
/// reversed has no such margin); refused where the averages would run off the waveform (in bytes, with one byte of
/// slack before the data) or a frame is not resident.
template <sample_read::frame_source S>
[[nodiscard]] std::optional<Averages> moving_averages(const S& source, int32_t start_frame,
                                                      int32_t crossfade_source_frames, Direction direction,
                                                      int32_t each) {
	const auto d = static_cast<int32_t>(direction);
	const int32_t length = source.length_frames();
	const int32_t half = crossfade_source_frames >> 1;
	const int32_t read = averages_read(start_frame, crossfade_source_frames, direction, each);
	if (direction == Direction::FORWARD && (read < half || read >= length - half)) {
		return std::nullopt;
	}
	// The legacy checks in bytes, with one byte of slack before the data: a frame index i is out where i × frame
	// bytes < -1, so a one-byte frame may end at -1
	const std::optional<sample_read::FrameRun> first = source.run(std::clamp(read, 0, length - 1), direction);
	if (!first) {
		return std::nullopt;
	}
	const int32_t frame_bytes = first->channels * first->byte_depth;
	const int32_t end = read + each * static_cast<int32_t>(std::tuple_size_v<Averages>) * d;
	if (end * frame_bytes < -1 || end > length) {
		return std::nullopt;
	}
	const FoldedFrames<S> folded(source);
	Averages totals{};
	int32_t frame = read;
	for (int32_t& total : totals) {
		if (frame * frame_bytes < -1 || frame >= length) {
			return std::nullopt;
		}
		for (int32_t k = 0; k < each; k++, frame += d) {
			const std::optional<int32_t> value = folded(frame);
			if (!value) {
				return std::nullopt;
			}
			total += *value;
		}
	}
	return totals;
}

/// @brief The most frames the search slides each way (A3): a share of the hop, repitched, held to a 45 Hz half-period
/// at @p sample_rate.
[[nodiscard]] constexpr int32_t search_limit(int32_t hop_frames, Increment increment, int32_t sample_rate) {
	int32_t limit = (hop_frames * 40) >> 8;
	limit = static_cast<int32_t>((static_cast<uint64_t>(static_cast<uint32_t>(limit)) * increment.raw()) >> 24);
	return std::min(limit, (sample_rate / 45) >> 1);
}

/// @brief One alignment's facts.
struct AlignRequest {
	int32_t start_frame{};
	Direction direction{Direction::FORWARD};
	Increment increment{};
	AlignSetup setup{};
	int32_t limit{};
	Averages old_totals{};
	UQ<24> older_phase{}; ///< The older head's phase, which a repitched alignment adds to (A5).
};

/// @brief Where the new head starts relative to its planned start, in frames, and its phase.
struct Alignment {
	int32_t offset_frames;
	UQ<24> phase;
};

/// @brief The search over @p source's frames in play order from a window's first frame.
template <sample_read::frame_source S>
class PlayOrderReader {
public:
	PlayOrderReader(const S& source, int32_t first_frame, Direction direction)
	    : source_(&source), frames_(source), first_(first_frame), d_(static_cast<int32_t>(direction)) {}
	[[nodiscard]] std::optional<int32_t> folded(int32_t frame) const { return frames_(first_ + frame * d_); }
	/// Frames from the leading read's frame to the waveform's edge in the slide's direction, the read included.
	[[nodiscard]] int32_t reach(int32_t direction, int32_t first_read) const {
		const int32_t at = first_ + first_read * d_;
		const int32_t available = direction * d_ == 1 ? source_->length_frames() - at : at + 1;
		return std::max(available, 0);
	}

private:
	const S* source_;
	FoldedFrames<S> frames_;
	int32_t first_;
	int32_t d_;
};

/// @brief Aligns a new head (A4, A5): its moving averages, nullopt where A2 refuses them, then the shared search in
/// play order, both ways, stopping after four sign flips. A repitched alignment adds the older head's phase, carrying a
/// whole frame. An abandoned search starts the head where it was planned, at phase 0 (decision 7).
template <sample_read::frame_source S>
[[nodiscard]] std::optional<Alignment> align(const S& source, const AlignRequest& request) {
	const std::optional<Averages> new_totals = moving_averages(
	    source, request.start_frame, request.setup.crossfade_source_frames, request.direction, request.setup.each);
	if (!new_totals) {
		return std::nullopt;
	}
	const bool repitched = request.increment.raw() != (1u << 24);
	const PlayOrderReader<S> reader(source,
	                                averages_read(request.start_frame, request.setup.crossfade_source_frames,
	                                              request.direction, request.setup.each),
	                                request.direction);
	const SearchShape shape{.flips_to_stop = 4, .first_direction = 1, .turns = true, .sub_sample = repitched};
	const SearchResult found =
	    search(reader, shape, request.old_totals, *new_totals, request.setup.each, request.limit);
	if (found.abandoned) {
		return Alignment{.offset_frames = 0, .phase = UQ<24>::from_raw(0)};
	}
	int32_t offset = found.offset;
	uint32_t phase = 0;
	if (repitched) {
		phase = static_cast<uint32_t>(found.phase) + request.older_phase.raw();
		if (phase >= (1u << 24)) {
			phase -= 1u << 24;
			offset += 1;
		}
	}
	return Alignment{.offset_frames = offset * static_cast<int32_t>(request.direction),
	                 .phase = UQ<24>::from_raw(phase)};
}

/// @brief The new head's start after alignment, held to the waveform's first frame in @p direction (A6); the start
/// clamp (B8) follows it.
[[nodiscard]] constexpr int32_t aligned_start(int32_t start_frame, const Alignment& alignment, Direction direction,
                                              int32_t length_frames) {
	const auto d = static_cast<int32_t>(direction);
	const int32_t start = start_frame + alignment.offset_frames;
	const int32_t first = direction == Direction::FORWARD ? 0 : length_frames - 1;
	return (start - first) * d < 0 ? first : start;
}

} // namespace deluge::dsp::fixed::timestretch
