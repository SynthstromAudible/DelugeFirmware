/*
 * Copyright © 2015-2026 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "dsp-next/core/stereo_sample.h"
#include "util/fixedpoint.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace deluge::dsp::fixed {

/// @brief A delay's tape over storage it does not own: the read and write heads, native or resampling (16.16
/// strengths between neighbouring samples), and the wraps the heads report. The delay and the stutterer both drive it.
///
/// The storage is a ring of sizeIncludingExtra() samples. The read head walks it one sample per step; the write head
/// trails kWriteLag samples behind the read head. Played at its native rate the tape steps once per output sample.
/// At any other rate it resamples: a long position (8.24, 1 is one step) advances by the rate relative to the native
/// one, the read head steps whenever the long position's whole part changes, and the 16-bit fraction between the
/// sample under the head and the next is the strength the caller interpolates and writes with.
///
/// Rates are on the delay's rate scale, where 16777216 (kMaxSampleValue) is a tape of kNeutralSize samples.
class DelayTape {
public:
	/// The largest tape, in samples: two seconds at 44.1 kHz.
	static constexpr size_t kMaxSize = 88200;
	/// The smallest tape, in samples.
	static constexpr size_t kMinSize = 1;
	/// The tape's size at the neutral rate (16777216), in samples.
	static constexpr size_t kNeutralSize = 16384;
	/// How far the write head trails the read head, in samples: the storage's extra beyond the played size.
	static constexpr std::ptrdiff_t kWriteLag = 20;

	/// @brief The read head: pointer, 8.24 long position, last short position.
	struct Head {
		StereoSample* pos;      ///< the sample under the read head
		uint32_t long_pos;      ///< the resampling position, 8.24: the whole part counts steps modulo 256
		uint8_t last_short_pos; ///< the long position's whole part at the last step
	};

	DelayTape() = default; ///< no storage: inactive

	/// @brief A tape over @p storage, the first @p size samples played and the rest the resampled write's run-off,
	/// at @p native_rate (1 is 16777216).
	///
	/// The tape starts as clear() leaves it. Only the samples clear() zeroes are cleared: a caller reads nothing
	/// else until the read head has passed over it once, clearing as it goes.
	DelayTape(std::span<StereoSample> storage, size_t size, uint32_t native_rate);

	/// @return The tape size for a delay at @p rate, and whether it was clamped to [kMinSize, kMaxSize].
	///
	/// The size is kNeutralSize * 16777216 / @p rate, truncated to 32 bits before it is clamped (so a rate of 128 or
	/// less can wrap to a size of 0 or a negative one, which clamps to kMinSize or kMaxSize).
	[[nodiscard]] static std::pair<int32_t, bool> idealSizeForRate(uint32_t rate);

	/// @return Whether the tape has storage.
	[[nodiscard]] constexpr bool isActive() const { return start_ != nullptr; }
	/// @return Whether the tape plays at its native rate: one step per sample.
	[[nodiscard]] constexpr bool isNative() const { return !resample_.has_value(); }
	/// @return Whether the tape resamples.
	[[nodiscard]] constexpr bool resampling() const { return resample_.has_value(); }
	/// @return The rate at which the tape plays natively (1 is 16777216).
	[[nodiscard]] constexpr uint32_t nativeRate() const { return native_rate_; }
	/// @return The played size, in samples.
	[[nodiscard]] constexpr size_t size() const { return size_; }
	/// @return The storage's size, in samples: the played size plus kWriteLag.
	[[nodiscard]] constexpr size_t sizeIncludingExtra() const { return size_including_extra_; }
	/// @return The storage's first sample.
	[[nodiscard]] constexpr StereoSample* begin() const { return start_; }
	/// @return One past the storage's last sample.
	[[nodiscard]] constexpr StereoSample* end() const { return end_; }
	/// @return The sample under the read head.
	[[nodiscard]] constexpr StereoSample& current() const { return *current_; }
	/// @brief Puts the read head on @p sample, which must lie in the storage.
	constexpr void setCurrent(StereoSample* sample) { current_ = sample; }

	/// @brief Sets up the resampling for a block at @p rate (native when it equals the native rate).
	///
	/// A tape that is native stays native at its native rate, and an inactive one never starts resampling. One that
	/// starts resampling first splits the sample last written, so the resampled writes' triangles mesh with it. Once
	/// resampling, a tape stays resampling (at the native rate too) until clear().
	void setupForRender(int32_t rate);

	/// @brief Sets the native rate to the one whose ideal size is exactly this tape's size.
	void makeNativeRatePrecise();

	/// @brief Sets the native rate to the one that plays this tape in the same time as @p other at @p other's
	/// native rate.
	void makeNativeRatePreciseRelativeTo(const DelayTape& other);

	/// @brief Puts the read head kWriteLag samples in and zeroes the storage up to two samples past it; ends any
	/// resampling. The rest of the storage is left: the read head clears it on its first pass.
	void clear();

	/// @return Whether the read head wrapped, after clearing the sample it leaves.
	bool clearAndMoveOn() {
		*current_ = {.l = 0, .r = 0};
		return moveOn();
	}

	/// @return Whether the read head wrapped.
	bool moveOn() {
		++current_;
		const bool wrapped = current_ == end_;
		if (wrapped) {
			current_ = start_;
		}
		return wrapped;
	}

	/// @return Whether the read head wrapped backwards.
	bool moveBack() {
		if (current_ == start_) {
			current_ = end_ - 1;
			return true;
		}
		--current_;
		return false;
	}

	/// @brief Advances the long position by one output sample at the resampling rate. The tape must be resampling.
	/// @return The resampling strength toward the next sample (16.16: 0 to 65535, 65536 being a whole sample),
	/// calling @p on_step per sample passed.
	template <typename OnStep>
	[[nodiscard, gnu::always_inline]] int32_t advance(OnStep&& on_step) {
		long_pos_ += resample_->actual_spin_rate;
		const auto new_short_pos = static_cast<uint8_t>(long_pos_ >> 24);
		auto steps = static_cast<uint8_t>(new_short_pos - last_short_pos_);
		last_short_pos_ = new_short_pos;
		for (; steps > 0; --steps) {
			on_step();
		}
		return static_cast<int32_t>((long_pos_ >> 8) & 65535);
	}

	/// @return Likewise, moving backwards.
	template <typename OnStep>
	[[nodiscard, gnu::always_inline]] int32_t retreat(OnStep&& on_step) {
		long_pos_ -= resample_->actual_spin_rate;
		const auto new_short_pos = static_cast<uint8_t>(long_pos_ >> 24);
		auto steps = static_cast<uint8_t>(last_short_pos_ - new_short_pos);
		last_short_pos_ = new_short_pos;
		for (; steps > 0; --steps) {
			on_step();
		}
		return static_cast<int32_t>((long_pos_ >> 8) & 65535);
	}

	/// @brief Writes @p sample (Q31) kWriteLag samples behind the read head.
	void writeNative(StereoSample sample) { *writePosition(0) = sample; }

	/// @brief Writes @p sample (Q31) at @p write_pos, then moves @p write_pos on, wrapping at the storage's end.
	void writeNativeAndMoveOn(StereoSample sample, StereoSample*& write_pos) {
		*write_pos = sample;
		++write_pos;
		if (write_pos == end_) {
			write_pos = start_;
		}
	}

	/// @brief A write at the head, native or resampled by @p strength1 and @p strength2 (which sum to 65536).
	[[gnu::always_inline]] void write(StereoSample sample, int32_t strength1, int32_t strength2) {
		if (isNative()) {
			writeNative(sample);
			return;
		}
		writeResampled(sample, strength1, strength2);
	}

	/// @brief Adds @p sample (Q31) into the storage around the write head, spread as a triangle so a resampled tape
	/// neither aliases nor changes level. @p strength1 and @p strength2 (16.16, summing to 65536) weigh the samples
	/// either side of the write head's long position. Nothing is written by a native tape.
	[[gnu::always_inline]] void writeResampled(StereoSample sample, int32_t strength1, int32_t strength2) {
		if (!resample_) {
			return;
		}
		if (resample_->actual_spin_rate >= static_cast<uint32_t>(kMaxSampleValue)) {
			writeSpedUp(sample, strength2);
		}
		else {
			writeSlowedDown(sample, strength1, strength2);
		}
	}

	/// @return The read head: pointer, 8.24 long position, last short position.
	[[nodiscard]] constexpr Head head() const {
		return {.pos = current_, .long_pos = long_pos_, .last_short_pos = last_short_pos_};
	}

	/// @brief Puts the read head back where @p head says.
	constexpr void setHead(Head head) {
		current_ = head.pos;
		long_pos_ = head.long_pos;
		last_short_pos_ = head.last_short_pos;
	}

private:
	/// How a resampling tape writes, for one rate.
	struct Resample {
		/// The tape's rate relative to its native rate, 8.24: 16777216 is native.
		uint32_t actual_spin_rate;
		/// The rate the sped-up write's triangle is sized for, twice actual_spin_rate held to at most 8 times native
		/// (8.24). 0 when slowed down.
		uint32_t spin_rate_for_sped_up_writing;
		/// The inverse rate, 16.16; halved when sped up, where it sets the triangle's slope.
		uint32_t divide_by_rate;
		/// When slowed down: the smallest multiple of the rate (16.16) that is at least one step, so the triangles
		/// meet. 0 when sped up.
		uint32_t rate_multiple;
		/// When slowed down: the gain that keeps the widened triangles at unity level. 0 when sped up.
		uint32_t write_size_adjustment;
	};

	/// @brief Starts resampling: the long position back to 0, and a quarter of the sample last written moved one
	/// sample on, where the triangles' writes will meet it.
	void setupResample();

	/// @return The sample @p offset past the write head (kWriteLag behind the read head), wrapped into the storage.
	[[nodiscard]] StereoSample* writePosition(std::ptrdiff_t offset) const {
		const auto ring = static_cast<std::ptrdiff_t>(size_including_extra_);
		std::ptrdiff_t index = (current_ - start_) - kWriteLag + offset;
		while (index < 0) {
			index += ring;
		}
		while (index >= ring) {
			index -= ring;
		}
		return start_ + index;
	}

	/// @return @p pos moved back one sample, wrapping at the storage's start.
	[[nodiscard]] StereoSample* before(StereoSample* pos) const { return pos == start_ ? end_ - 1 : pos - 1; }

	/// @brief A write at above the native rate: a triangle at least two steps either side of the long position,
	/// from its far right to where its left side falls to nothing. Each write is the sample at the strength the
	/// triangle has there (4.28), shifted back up by 3.
	[[gnu::always_inline]] void writeSpedUp(StereoSample sample, int32_t strength2) {
		const auto far_right = static_cast<int32_t>(
		    (static_cast<uint32_t>(strength2) + (resample_->spin_rate_for_sped_up_writing >> 8)) >> 16);
		// The distance from the write head, 16.16: one sample is 65536.
		int32_t distance = far_right << 16;
		StereoSample* pos = writePosition(far_right);

		auto strengthAt = [&](int32_t from_long_pos) {
			return static_cast<int32_t>(0x0FFFFFFFu
			                            - (static_cast<uint32_t>(from_long_pos >> 4) * resample_->divide_by_rate));
		};
		auto add = [&](int32_t strength) {
			pos->l += multiply_32x32_rshift32(sample.l, strength) << 3;
			pos->r += multiply_32x32_rshift32(sample.r, strength) << 3;
			pos = before(pos);
		};

		// The right side, up to the write head.
		while (distance != 0) {
			add(strengthAt(distance - strength2));
			distance -= 65536;
		}
		// The left side, from the write head until the triangle reaches nothing.
		while (true) {
			const int32_t strength = strengthAt(distance + strength2);
			if (strength <= 0) {
				break;
			}
			add(strength);
			distance += 65536;
		}
	}

	/// @brief A write at below the native rate: four samples, from two right of the write head back to one left of
	/// it, each at its side's strength (16.16) widened by the rate multiple and scaled by the write size adjustment.
	[[gnu::always_inline]] void writeSlowedDown(StereoSample sample, int32_t strength1, int32_t strength2) {
		const auto widen = [&](int32_t strength) {
			return static_cast<int32_t>(static_cast<uint32_t>(strength) + resample_->rate_multiple - 65536);
		};
		const int32_t main = widen(strength1);
		const int32_t next = widen(strength2);
		// Right to left: two past the write head, one past it, the write head, one before it.
		const std::array<int32_t, 4> strengths{next - 65536, next, main, main - 65536};

		StereoSample* pos = writePosition(2);
		for (size_t i = 0; i < strengths.size(); ++i) {
			const int32_t strength = strengths[i];
			if (strength > 0) {
				const auto gain =
				    static_cast<int32_t>(static_cast<uint32_t>(strength >> 2) * resample_->write_size_adjustment);
				pos->l += multiply_32x32_rshift32(sample.l, gain) << 2;
				pos->r += multiply_32x32_rshift32(sample.r, gain) << 2;
			}
			if (i + 1 < strengths.size()) {
				pos = before(pos);
			}
		}
	}

	uint32_t native_rate_ = 0;
	StereoSample* start_ = nullptr;
	StereoSample* end_ = nullptr;
	StereoSample* current_ = nullptr;
	size_t size_ = 0;
	size_t size_including_extra_ = 0;
	/// The resampling position, 8.24: see Head.
	uint32_t long_pos_ = 0;
	/// The long position's whole part at the last step.
	uint8_t last_short_pos_ = 0;
	std::optional<Resample> resample_{};
};

} // namespace deluge::dsp::fixed
