/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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
#include "dsp/stereo_sample.h"
#include <cstdint>
#include <expected>
#include <optional>

class StereoSample;

constexpr ptrdiff_t delaySpaceBetweenReadAndWrite = 20;

class DelayBuffer {
public:
	constexpr static size_t kMaxSize = 88200;         // 2 seconds - for delay effect
	constexpr static size_t kStutterMaxSize = 264600; // 6 seconds - for stutter/scatter
	constexpr static size_t kMinSize = 1;
	constexpr static size_t kNeutralSize = 16384;

	DelayBuffer() = default;
	~DelayBuffer() { discard(); }
	Error init(uint32_t newRate, uint32_t failIfThisSize = 0, bool includeExtraSpace = true);
	Error initWithSize(size_t sampleCount, bool includeExtraSpace = true);

	// Prevent the delaybuffer from deallocing the Sample array on destruction
	// TODO (Kate): investigate a shared_ptr for start_
	constexpr void invalidate() { start_ = nullptr; }

	void makeNativeRatePrecise();
	void makeNativeRatePreciseRelativeToOtherBuffer(const DelayBuffer& otherBuffer);

	void discard();

	template <typename C>
	[[gnu::always_inline]] constexpr int32_t advance(C callback) {
		longPos += resample_config_.value().actualSpinRate;
		uint8_t newShortPos = longPos >> 24;
		uint8_t shortPosDiff = newShortPos - lastShortPos;
		lastShortPos = newShortPos;

		while (shortPosDiff > 0) {
			callback();
			shortPosDiff--;
		}
		return (longPos >> 8) & 65535;
	}
	template <typename C>
	[[gnu::always_inline]] constexpr int32_t retreat(C callback) {
		longPos -= resample_config_.value().actualSpinRate;
		uint8_t newShortPos = longPos >> 24;
		uint8_t shortPosDiff = lastShortPos - newShortPos; // backward diff
		lastShortPos = newShortPos;

		while (shortPosDiff > 0) {
			callback();
			shortPosDiff--;
		}
		return (longPos >> 8) & 65535; // return pos
	}

	void setupForRender(int32_t rate);

	static std::pair<int32_t, bool> getIdealBufferSizeFromRate(uint32_t rate);

	[[nodiscard]] constexpr bool isActive() const { return (start_ != nullptr); }

	inline bool clearAndMoveOn() {
		current_->l = 0;
		current_->r = 0;
		return moveOn();
	}

	inline bool moveOn() {
		++current_;
		bool wrapped = (current_ == end_);
		if (wrapped) {
			current_ = start_;
		}
		return wrapped;
	}

	inline bool moveBack() {
		if (current_ == start_) {
			current_ = end_ - 1; // wrap around to last element
			return true;
		}
		else {
			--current_;   // move back
			return false; // no wrap
		}
	}

	inline void writeNative(StereoSample toDelay) {
		StereoSample* writePos = current_ - delaySpaceBetweenReadAndWrite;
		if (writePos < start_) {
			writePos += sizeIncludingExtra;
		}
		writePos->l = toDelay.l;
		writePos->r = toDelay.r;
	}

	inline void writeNativeAndMoveOn(StereoSample toDelay, StereoSample** writePos) {
		(*writePos)->l = toDelay.l;
		(*writePos)->r = toDelay.r;

		(*writePos)++;
		if (*writePos == end_) {
			*writePos = start_;
		}
	}

	[[gnu::always_inline]] void write(StereoSample toDelay, int32_t strength1, int32_t strength2) {
		// If no speed adjustment
		if (isNative()) {
			StereoSample* writePos = current_ - delaySpaceBetweenReadAndWrite;
			if (writePos < start_) {
				writePos += sizeIncludingExtra;
			}
			writePos->l = toDelay.l;
			writePos->r = toDelay.r;
			return;
		}

		writeResampled(toDelay, strength1, strength2);
	}

	[[gnu::always_inline]] void writeResampled(StereoSample toDelay, int32_t strength1, int32_t strength2) {
		if (!resample_config_) {
			return;
		}
		// If delay buffer spinning above sample rate...
		if (resample_config_->actualSpinRate >= kMaxSampleValue) {

			// An improvement on that could be to only do the triangle-widening when we're down near the native rate -
			// i.e. set a minimum width of double the native rate rather than always doubling the width. The difficulty
			// would be ensuring that we compensate perfectly the strength of each write so that the volume remains the
			// same. It could totally be done. The only real advantage would be that the number of memory writes would
			// be halved at high speeds.

			// For efficiency, we start far-right, then traverse to far-left.

			// I rearranged some algebra to get this from the strengthThisWrite equation
			int32_t howFarRightToStart = (strength2 + (resample_config_->spinRateForSpedUpWriting >> 8)) >> 16;

			// This variable represents one "step" of the delay buffer as 65536.
			// Always positive - absolute distance
			int32_t distanceFromMainWrite = (int32_t)howFarRightToStart << 16;

			// Initially is the far-right right pos, not the central "main" one
			StereoSample* writePos = current_ - delaySpaceBetweenReadAndWrite + howFarRightToStart;
			while (writePos < start_) {
				writePos += sizeIncludingExtra;
			}
			while (writePos >= end_) {
				writePos -= sizeIncludingExtra;
			}

			// Do all writes to the right of the main write pos
			while (distanceFromMainWrite != 0) { // For as long as we haven't reached the "main" pos...
				// Check my notebook for a rudimentary diagram
				int32_t strengthThisWrite =
				    (0xFFFFFFFF >> 4) - (((distanceFromMainWrite - strength2) >> 4) * resample_config_->divideByRate);

				writePos->l += multiply_32x32_rshift32(toDelay.l, strengthThisWrite) << 3;
				writePos->r += multiply_32x32_rshift32(toDelay.r, strengthThisWrite) << 3;

				if (--writePos < start_) {
					writePos = end_ - 1;
				}
				// writePos = (writePos == start_) ? end_ - 1 : writePos - 1;
				distanceFromMainWrite -= 65536;
			}

			// Do all writes to the left of (and including) the main write pos
			while (true) {
				int32_t strengthThisWrite =
				    (0xFFFFFFFF >> 4) - (((distanceFromMainWrite + strength2) >> 4) * resample_config_->divideByRate);
				if (strengthThisWrite <= 0) {
					break; // And stop when we've got far enough left that we shouldn't be squirting any more juice here
				}

				writePos->l += multiply_32x32_rshift32(toDelay.l, strengthThisWrite) << 3;
				writePos->r += multiply_32x32_rshift32(toDelay.r, strengthThisWrite) << 3;

				--writePos;

				// loop around
				if (writePos < start_) {
					writePos = end_ - 1;
				}
				distanceFromMainWrite += 65536;
			}
		}

		// Or if delay buffer spinning below sample rate...
		else {
			// The most basic version of this would be to write to the "main" pos with strength1 and the "main + 1" pos
			// with strength 2. But this isn't immune to aliasing, so we instead "squirt" things a little bit wider -
			// wide enough that our "triangle" is always at least as wide as 1 step / sample in the delay buffer. This
			// means we potentially need a further 1 write in each direction.

			// And because we're "arbitrarily" increasing the width and height (height is more a side-effect of the
			// simple algorithm) of our squirt, and also how spaced out the squirts are, the value written at each step
			// needs to be resized. See delayWriteSizeAdjustment, which is calculated above.

			// We've also had to make sure that the "triangles"' corners exactly meet up. Unfortunately this means even
			// a tiny slow-down causes half the bandwidth to be lost

			// The furthest right we need to write is 2 steps right from the "main" write
			StereoSample* writePos = current_ - delaySpaceBetweenReadAndWrite + 2;

			while (writePos < start_) {
				writePos += sizeIncludingExtra;
			}

			// Not needed - but be careful! Leave this here as a reminder
			// while (writePos >= end_) writePos -= sizeIncludingExtra;

			int32_t strength[4];

			strength[1] = strength1 + resample_config_->rateMultiple - 65536; // For the "main" pos
			strength[2] = strength2 + resample_config_->rateMultiple - 65536; // For the "main + 1" pos

			// Strengths for the further 1 write in each direction
			strength[0] = strength[1] - 65536;
			strength[3] = strength[2] - 65536;

			int8_t i = 3;
			while (true) {
				if (strength[i] > 0) {
					writePos->l +=
					    multiply_32x32_rshift32(toDelay.l, (strength[i] >> 2) * resample_config_->writeSizeAdjustment)
					    << 2;
					writePos->r +=
					    multiply_32x32_rshift32(toDelay.r, (strength[i] >> 2) * resample_config_->writeSizeAdjustment)
					    << 2;
				}
				if (--i < 0) {
					break;
				}

				--writePos;

				// loop around
				if (writePos < start_) {
					writePos = end_ - 1;
				}
			}
		}
	}

	[[nodiscard]] constexpr bool isNative() const { return !resample_config_.has_value(); }
	[[nodiscard]] constexpr bool resampling() const { return resample_config_.has_value(); }
	[[nodiscard]] constexpr uint32_t nativeRate() const { return native_rate_; }

	// Iterator access
	[[nodiscard]] constexpr StereoSample& current() const { return *current_; }
	[[nodiscard]] constexpr StereoSample* begin() const { return start_; }
	[[nodiscard]] constexpr StereoSample* end() const { return end_; }
	[[nodiscard]] constexpr size_t size() const { return size_; }

	void clear();

	constexpr void setCurrent(StereoSample* sample) { current_ = sample; }

	// TODO (Kate): Make it so the ModControllableFX stuff isn't touching these.
	// That behavior should either be contained in this class or Delay or a new Stutterer class
	uint32_t longPos;
	uint8_t lastShortPos;

	size_t sizeIncludingExtra;

private:
	struct ResampleConfig {
		uint32_t actualSpinRate;           // 1 is represented as 16777216
		uint32_t spinRateForSpedUpWriting; // Normally the same as actualSpinRate, but subject to some limits for safety
		uint32_t divideByRate;             // 1 is represented as 65536
		uint32_t rateMultiple;
		uint32_t writeSizeAdjustment;
	};

	void setupResample();

	uint32_t native_rate_ = 0;

	StereoSample* start_ = nullptr;
	StereoSample* end_;
	StereoSample* current_;

	size_t size_;

public:
	std::optional<ResampleConfig> resample_config_{};
};
