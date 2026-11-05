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

#include "definitions_cxx.hpp" // kInputRawBufferSize, kInputPercBufferSize, kPercBufferReductionMagnitude
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/pitch_shift/slope_follower.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

/// @file
/// @brief The live input's recent history: its frames in a ring, and a percussiveness measure every 128 frames.

namespace deluge::dsp::fixed::pitch_shift {

/// Frames of history kept: the ring's length, a power of two.
inline constexpr uint32_t kHistoryFrames = kInputRawBufferSize;
/// Frames per percussiveness entry, as a power of two: one entry every 128 frames.
inline constexpr int kPercussivenessStepMagnitude = kPercBufferReductionMagnitude;
/// Percussiveness entries kept: one per 128 frames of the ring.
inline constexpr uint32_t kPercussivenessEntries = kInputPercBufferSize;
/// The moving averages a hop's alignment compares.
inline constexpr std::size_t kMovingAverages = TimeStretch::Crossfade::kNumMovingAverages;

/// @brief A position in the history's ring, wrapping at its length.
class RingPosition {
public:
	constexpr RingPosition() = default;
	/// @brief The position @p index names, wrapped into the ring.
	constexpr explicit RingPosition(uint32_t index) : index_(index & (kHistoryFrames - 1)) {}
	/// @brief The index in the ring, 0 to kHistoryFrames − 1.
	[[nodiscard]] constexpr uint32_t index() const { return index_; }
	/// @brief The position @p frames later, wrapping.
	[[nodiscard]] constexpr RingPosition operator+(int32_t frames) const {
		return RingPosition(index_ + static_cast<uint32_t>(frames));
	}
	/// @brief The position @p frames earlier, wrapping.
	[[nodiscard]] constexpr RingPosition operator-(int32_t frames) const {
		return RingPosition(index_ - static_cast<uint32_t>(frames));
	}
	/// @brief The frames from @p earlier forward to this position, 0 to kHistoryFrames − 1.
	[[nodiscard]] constexpr uint32_t since(RingPosition earlier) const {
		return (index_ - earlier.index_) & (kHistoryFrames - 1);
	}
	constexpr bool operator==(const RingPosition&) const = default;

private:
	uint32_t index_ = 0;
};

/// @brief The totals of kMovingAverages runs of frames' high words, back to back.
using MovingAverages = std::array<int32_t, kMovingAverages>;

/// @brief The live input's recent history: its frames, and how percussive it was.
/// @details Holds the last kHistoryFrames frames in a ring and, every 128 frames, the percussiveness (0 to 63) of the
/// input's smoothed slope there, read from a @ref SlopeFollower stepped over the input folded to one channel (a quarter
/// of each channel, summed). The count of frames written runs on past the ring's length, wrapping at 2^32.
/// @tparam Frame Q<31> for one channel, StereoSample for two.
template <typename Frame>
requires std::is_same_v<Frame, Q<31>> || std::is_same_v<Frame, StereoSample>
class InputHistory {
public:
	/// @brief Forgets the input: no frames written and the follower at rest. The ring keeps its contents, which the
	/// count says are not yet written.
	void restart() {
		frames_written_ = 0;
		follower_ = {};
	}

	/// @brief Appends @p n frames from @p source, one pass per frame: each is stored, folded and followed, and every
	/// 128th frame's percussiveness noted.
	/// @tparam Source An object whose `next()` gives the next frame.
	template <typename Source>
	void write(Source& source, uint32_t n) {
		Q<31> previous = SlopeFollower::slope(follower_);
		for (uint32_t i = 0; i < n; ++i) {
			const Frame frame = source.next();
			frames_[frames_written_ & (kHistoryFrames - 1)] = frame;
			const Q<31> slope = kernel_ops::step_at<SlopeFollower, kernel_ops::NoMode::NONE>({}, EmptyBlock{},
			                                                                                 follower_, folded(frame));
			if ((frames_written_ & ((1U << kPercussivenessStepMagnitude) - 1)) == 0) {
				percussiveness_[(frames_written_ >> kPercussivenessStepMagnitude) & (kPercussivenessEntries - 1)] =
				    percussiveness(slope, previous);
			}
			previous = slope;
			++frames_written_;
		}
	}

	/// @brief Appends @p frames.
	void write(std::span<const Frame> frames) {
		SpanSource source{frames};
		write(source, static_cast<uint32_t>(frames.size()));
	}

	/// @brief The frames written since the last restart, wrapping at 2^32.
	[[nodiscard]] uint32_t frames_written() const { return frames_written_; }
	/// @brief The position the next frame is written to.
	[[nodiscard]] RingPosition now() const { return RingPosition(frames_written_); }

	/// @brief The frame at @p p.
	[[nodiscard]] const Frame& at(RingPosition p) const { return frames_[p.index()]; }

	/// @brief The top half of the frame's left (or only) channel at @p p, Q15: what the interpolator's taps take.
	[[nodiscard]] Q16<15> high_l(RingPosition p) const { return to_q16_sat(left(at(p))); }
	/// @brief The top half of the frame's right channel at @p p, Q15.
	[[nodiscard]] Q16<15> high_r(RingPosition p) const
	requires std::is_same_v<Frame, StereoSample>
	{
		return to_q16_sat(at(p).r);
	}

	/// @brief The frame's channels' top halves at @p p, summed as integers: the alignment's measure of the waveform.
	[[nodiscard]] int32_t folded_high_word(RingPosition p) const {
		// Words: the alignment sums the halves as integers
		if constexpr (std::is_same_v<Frame, StereoSample>) {
			return int32_t{high_l(p).raw()} + int32_t{high_r(p).raw()};
		}
		else {
			return high_l(p).raw();
		}
	}

	/// @brief The totals of the frames' folded high words over kMovingAverages runs of @p length frames each, back to
	/// back from @p start.
	/// @param length At least 1.
	[[nodiscard]] MovingAverages moving_averages(RingPosition start, int32_t length) const {
		MovingAverages totals{};
		RingPosition p = start;
		for (int32_t& total : totals) {
			const RingPosition end = p + length;
			do {
				total += folded_high_word(p);
				p = p + 1;
			} while (p != end);
		}
		return totals;
	}

	/// @brief The percussiveness entry @p entry, wrapping at kPercussivenessEntries.
	[[nodiscard]] uint8_t percussiveness_at(uint32_t entry) const {
		return percussiveness_[entry & (kPercussivenessEntries - 1)];
	}

	/// @brief Whether @p p holds a frame written since the last restart: its index is below the count written.
	/// @details Exact until the ring has wrapped, and true everywhere after it.
	[[nodiscard]] bool written(RingPosition p) const { return p.index() < frames_written_; }

private:
	/// @brief Frames from a span, in order.
	struct SpanSource {
		std::span<const Frame> frames;
		std::size_t at = 0;
		[[nodiscard]] Frame next() { return frames[at++]; }
	};

	[[nodiscard]] static constexpr Q<31> left(const Frame& f) {
		if constexpr (std::is_same_v<Frame, StereoSample>) {
			return f.l;
		}
		else {
			return f;
		}
	}

	/// @brief The frame folded to one channel for the follower: a quarter of each channel, summed.
	[[nodiscard]] static constexpr Q<31> folded(const Frame& f) {
		if constexpr (std::is_same_v<Frame, StereoSample>) {
			return add_wrap(scale_shr_floor<2>(f.l), scale_shr_floor<2>(f.r));
		}
		else {
			return scale_shr_floor<2>(f);
		}
	}

	std::array<Frame, kHistoryFrames> frames_{};
	/// Zeroed: a hop reads entries ahead of what has been written while the input warms up.
	std::array<uint8_t, kPercussivenessEntries> percussiveness_{};
	SlopeFollower::State follower_{};
	uint32_t frames_written_ = 0;
};

} // namespace deluge::dsp::fixed::pitch_shift
