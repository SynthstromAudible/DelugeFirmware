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
#include "memory/general_memory_allocator.h"
#include "processing/engines/audio_engine.h"
#include "util/functions.h"
#include <string.h>

DelayBuffer::DelayBuffer() {
	bufferStart = 0;
}

DelayBuffer::~DelayBuffer() {
	discard(true);
}

// Returns error status
uint8_t DelayBuffer::init(uint32_t newRate, uint32_t failIfThisSize, bool includeExtraSpace) {

	//Uart::println("init buffer");
	nativeRate = newRate;
	size = getIdealBufferSizeFromRate(nativeRate);

	//Uart::print("buffer size: ");
	//Uart::println(size);

	bool mustMakeRatePrecise = false;

	if (size > DELAY_BUFFER_MAX_SIZE) {
		size = DELAY_BUFFER_MAX_SIZE;
		mustMakeRatePrecise = true;
	}

	if (size < DELAY_BUFFER_MIN_SIZE) {
		size = DELAY_BUFFER_MIN_SIZE;
		mustMakeRatePrecise = true;
	}

	if (size == failIfThisSize) {
		return ERROR_UNSPECIFIED;
	}

	if (mustMakeRatePrecise) {
		makeNativeRatePrecise();
	}

	sizeIncludingExtra = size + (includeExtraSpace ? delaySpaceBetweenReadAndWrite : 0);
	AudioEngine::logAction("DelayBuffer::init before");

	bufferStart = (StereoSample*)GeneralMemoryAllocator::get().allocLowSpeed(sizeIncludingExtra * sizeof(StereoSample));
	AudioEngine::logAction("DelayBuffer::init after");
	if (bufferStart == 0) {
		return ERROR_INSUFFICIENT_RAM;
	}

	bufferEnd = bufferStart + sizeIncludingExtra;
	empty();
	return NO_ERROR;
}

void DelayBuffer::empty() {
	memset(bufferStart, 0, sizeof(StereoSample) * (delaySpaceBetweenReadAndWrite + 2));
	bufferCurrentPos = bufferStart + delaySpaceBetweenReadAndWrite;
	isResampling = false;
}

int32_t DelayBuffer::getIdealBufferSizeFromRate(uint32_t newRate) {
	return (uint64_t)DELAY_BUFFER_NEUTRAL_SIZE * 16777216 / newRate;
}

void DelayBuffer::makeNativeRatePrecise() {
	nativeRate = round((double)DELAY_BUFFER_NEUTRAL_SIZE * (double)16777216 / (double)size);
}

void DelayBuffer::makeNativeRatePreciseRelativeToOtherBuffer(DelayBuffer* otherBuffer) {
	double otherBufferAmountTooFast = (double)otherBuffer->nativeRate * (double)otherBuffer->size
	                                  / ((double)DELAY_BUFFER_NEUTRAL_SIZE * (double)16777216);
	nativeRate = round((double)DELAY_BUFFER_NEUTRAL_SIZE * (double)16777216 * otherBufferAmountTooFast / (double)size);
}

void DelayBuffer::discard(bool beingDestructed) {
	if (bufferStart) {
		delugeDealloc(bufferStart);
		if (!beingDestructed) {
			bufferStart = NULL; // If destructing, writing anything would be a waste of time
		}
	}
}

void DelayBuffer::setupForRender(int32_t userDelayRate, DelayBufferSetup* setup) {
	if (userDelayRate != nativeRate) {
		if (!isResampling && bufferStart != 0) {
			isResampling = true;
			longPos = 0;
			lastShortPos = 0;

			// Because we're switching from direct writing to writing in "triangles", we need to adjust the data we last wrote directly so that it meshes
			// with the upcoming triangles. Assuming that the delay rate has only changed slightly at this stage, this is as simple as removing a quarter of the last written value,
			// and putting that removed quarter where the "next" write-pos is. That's because the triangles are 4 samples wide total (2 samples either side)
			StereoSample* writePos = bufferCurrentPos - delaySpaceBetweenReadAndWrite;
			while (writePos < bufferStart) {
				writePos += sizeIncludingExtra;
			}

			StereoSample* writePosPlusOne = writePos + 1;
			while (writePosPlusOne >= bufferEnd) {
				writePosPlusOne -= sizeIncludingExtra;
			}

			writePosPlusOne->l = writePos->l >> 2;
			writePosPlusOne->r = writePos->r >> 2;

			writePos->l -= writePosPlusOne->l;
			writePos->r -= writePosPlusOne->r;
		}
	}

	if (isResampling) {

		setup->actualSpinRate =
		    (uint64_t)((double)(userDelayRate << 24) / (double)nativeRate); // 1 is represented as 16777216
		setup->divideByRate =
		    (uint32_t)((double)0xFFFFFFFF / (double)(setup->actualSpinRate >> 8)); // 1 is represented as 65536

		// If buffer spinning slow
		if (setup->actualSpinRate < 16777216) {

			uint32_t timesSlowerRead = setup->divideByRate >> 16;

			// This seems to be the best option. speedMultiple is set to the smallest multiple of delay.speed which is greater than 65536.
			// Means the "triangles" link up, and are at least as wide as a frame of the write buffer. Does that make sense?
			setup->rateMultiple = (setup->actualSpinRate >> 8) * (timesSlowerRead + 1);

			// This was tricky to work out. Needs to go up with delay.speed because this means less "density". And squarely down with writeRateMultiple
			// because more of that means more "triangle area", or more stuff written each time.
			//uint32_t delayWriteSizeAdjustment2 = (((uint32_t)delay.speed << 16) / (((uint32_t)(speedMultiple >> 2) * (uint32_t)(speedMultiple >> 2)) >> 11));
			setup->writeSizeAdjustment =
			    (uint32_t)((double)0xFFFFFFFF
			               / (double)(setup->rateMultiple
			                          * (timesSlowerRead
			                             + 1))); // Equivalent to one order of magnitude bigger than the above line
		}

		// If buffer spinning fast
		else {

			// First, let's limit sped up writing to only work perfectly up to 8x speed, for safety (writing faster takes longer).
			// No need to adjust divideByRate to compensate - it's going to sound shoddy anyway
			setup->spinRateForSpedUpWriting = std::min(setup->actualSpinRate, (int32_t)16777216 * 8);

			// We want to squirt the most juice right at the "main" write pos - but we want to spread it wider too.
			// A basic version of this would involve the triangle's base being as wide as 2 samples if we were writing at the native sample rate.
			// However I've stretched the triangle twice as wide so that at the native sample rate it's the same width as the slowed-down algorithm below, so there's
			// no click when switching between the two. This does mean we lose half the bandwidth. That's done with the following 2 lines of code, and the fact that
			// the actual writes below are <<3 instead of <<4.
			setup->spinRateForSpedUpWriting = setup->spinRateForSpedUpWriting <<=
			    1;                     // Woah, did I mean to write "<<=" ?
			setup->divideByRate >>= 1; // We may change this because sped up writing is the only thing it'll be used for
		}
	}
}
