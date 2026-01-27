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

#include "dsp/delay/delay_buffer.h"
#include "dsp/stereo_sample.h"
#include "mem_functions.h"
#include "memory/memory_allocator_interface.h"
#include <cmath>
#include <optional>

// Returns error status
Error DelayBuffer::init(uint32_t rate, uint32_t failIfThisSize, bool includeExtraSpace) {

	// Uart::println("init buffer");
	auto [size, make_precise] = getIdealBufferSizeFromRate(rate);

	native_rate_ = rate;
	size_ = size;

	// Uart::print("buffer size_: ");
	// Uart::println(size_);

	if (size_ == failIfThisSize) {
		return Error::UNSPECIFIED;
	}

	if (make_precise) {
		makeNativeRatePrecise();
	}

	sizeIncludingExtra = size_ + (includeExtraSpace ? delaySpaceBetweenReadAndWrite : 0);

	start_ = (StereoSample*)allocLowSpeed(sizeIncludingExtra * sizeof(StereoSample));

	if (start_ == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}

	end_ = start_ + sizeIncludingExtra;
	clear();
	return Error::NONE;
}

Error DelayBuffer::initWithSize(size_t sampleCount, bool includeExtraSpace) {
	// Clamp to valid range
	if (sampleCount > kStutterMaxSize) {
		sampleCount = kStutterMaxSize;
	}
	if (sampleCount < kMinSize) {
		sampleCount = kMinSize;
	}

	size_ = sampleCount;
	// Calculate native rate from size (inverse of size calculation)
	// buffer_size = kNeutralSize * kMaxSampleValue / rate
	// rate = kNeutralSize * kMaxSampleValue / buffer_size
	native_rate_ = static_cast<uint32_t>((uint64_t)kNeutralSize * kMaxSampleValue / size_);

	sizeIncludingExtra = size_ + (includeExtraSpace ? delaySpaceBetweenReadAndWrite : 0);

	start_ = (StereoSample*)allocLowSpeed(sizeIncludingExtra * sizeof(StereoSample));

	if (start_ == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}

	end_ = start_ + sizeIncludingExtra;
	clear();
	return Error::NONE;
}

void DelayBuffer::clear() {
	memset(start_, 0, sizeof(StereoSample) * (delaySpaceBetweenReadAndWrite + 2));
	current_ = start_ + delaySpaceBetweenReadAndWrite;
	resample_config_ = std::nullopt;
}

std::pair<int32_t, bool> DelayBuffer::getIdealBufferSizeFromRate(uint32_t newRate) {
	int32_t buffer_size = (uint64_t)kNeutralSize * kMaxSampleValue / newRate;

	bool clamped = false;

	if (buffer_size > kMaxSize) {
		buffer_size = kMaxSize;
		clamped = true;
	}

	if (buffer_size < kMinSize) {
		buffer_size = kMinSize;
		clamped = true;
	}

	return std::make_pair(buffer_size, clamped);
}

void DelayBuffer::makeNativeRatePrecise() {
	native_rate_ = round((double)kNeutralSize * (double)kMaxSampleValue / (double)size_);
}

void DelayBuffer::makeNativeRatePreciseRelativeToOtherBuffer(const DelayBuffer& otherBuffer) {
	double otherBufferAmountTooFast =
	    (double)otherBuffer.native_rate_ * (double)otherBuffer.size_ / ((double)kNeutralSize * (double)kMaxSampleValue);
	native_rate_ = round((double)kNeutralSize * (double)kMaxSampleValue * otherBufferAmountTooFast / (double)size_);
}

void DelayBuffer::discard() {
	if (start_ != nullptr) {
		delugeDealloc(start_);
		start_ = nullptr;
	}
}

void DelayBuffer::setupResample() {
	// Prep for resample
	longPos = 0;
	lastShortPos = 0;

	// Because we're switching from direct writing to writing in "triangles", we need to adjust the data we last
	// wrote directly so that it meshes with the upcoming triangles. Assuming that the delay rate has only
	// changed slightly at this stage, this is as simple as removing a quarter of the last written value, and
	// putting that removed quarter where the "next" write-pos is. That's because the triangles are 4 samples
	// wide total (2 samples either side)
	StereoSample* writePos = current_ - delaySpaceBetweenReadAndWrite;
	while (writePos < start_) {
		writePos += sizeIncludingExtra;
	}

	StereoSample* writePosPlusOne = writePos + 1;
	while (writePosPlusOne >= end_) {
		writePosPlusOne -= sizeIncludingExtra;
	}

	writePosPlusOne->l = writePos->l >> 2;
	writePosPlusOne->r = writePos->r >> 2;

	writePos->l -= writePosPlusOne->l;
	writePos->r -= writePosPlusOne->r;
}

void DelayBuffer::setupForRender(int32_t rate) {
	if (!resampling()) {
		if (rate == native_rate_ || start_ == nullptr) {
			// Can't/won't resample if the rate is native or the buffer is discarded
			return;
		}

		// Resample _only_ if rate is not native and we have a valid buffer
		setupResample();
	}

	uint32_t actualSpinRate;
	uint32_t spinRateForSpedUpWriting;
	uint32_t divideByRate;
	uint32_t rateMultiple;
	uint32_t writeSizeAdjustment;

	actualSpinRate = (uint64_t)((double)((uint64_t)rate << 24) / (double)native_rate_); // 1 is represented as 16777216
	divideByRate = (uint32_t)((double)0xFFFFFFFF / (double)(actualSpinRate >> 8));      // 1 is represented as 65536

	// If buffer spinning slow
	if (actualSpinRate < kMaxSampleValue) {

		uint32_t timesSlowerRead = divideByRate >> 16;

		// This seems to be the best option. speedMultiple is set to the smallest multiple of delay.speed which is
		// greater than 65536. Means the "triangles" link up, and are at least as wide as a frame of the write
		// buffer. Does that make sense?
		rateMultiple = (actualSpinRate >> 8) * (timesSlowerRead + 1);

		// This was tricky to work out. Needs to go up with delay.speed because this means less "density". And
		// squarely down with writeRateMultiple because more of that means more "triangle area", or more stuff
		// written each time.
		// uint32_t delayWriteSizeAdjustment2 = (((uint32_t)delay.speed << 16) / (((uint32_t)(speedMultiple >> 2) *
		// (uint32_t)(speedMultiple >> 2)) >> 11));

		// Equivalent to one order of magnitude bigger than the above line
		writeSizeAdjustment = (uint32_t)((double)0xFFFFFFFF / (double)(rateMultiple * (timesSlowerRead + 1)));
	}

	// If buffer spinning fast
	else {

		// First, let's limit sped up writing to only work perfectly up to 8x speed, for safety (writing faster
		// takes longer). No need to adjust divideByRate to compensate - it's going to sound shoddy anyway
		spinRateForSpedUpWriting = std::min(actualSpinRate, (uint32_t)kMaxSampleValue * 8);

		// We want to squirt the most juice right at the "main" write pos - but we want to spread it wider too.
		// A basic version of this would involve the triangle's base being as wide as 2 samples if we were writing
		// at the native sample rate. However I've stretched the triangle twice as wide so that at the native sample
		// rate it's the same width as the slowed-down algorithm below, so there's no click when switching between
		// the two. This does mean we lose half the bandwidth. That's done with the following 2 lines of code, and
		// the fact that the actual writes below are <<3 instead of <<4.

		spinRateForSpedUpWriting <<= 1;
		// We may change this because sped up writing is the only thing it'll be used for
		divideByRate >>= 1;
	}
	resample_config_ = DelayBuffer::ResampleConfig{
	    .actualSpinRate = actualSpinRate,
	    .spinRateForSpedUpWriting = spinRateForSpedUpWriting,
	    .divideByRate = divideByRate,
	    .rateMultiple = rateMultiple,
	    .writeSizeAdjustment = writeSizeAdjustment,
	};
}
