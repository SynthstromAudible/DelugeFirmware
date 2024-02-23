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

#include "dsp/stereo_sample.h"
#include <cstdint>

class StereoSample;

#define delaySpaceBetweenReadAndWrite 20
#define DELAY_BUFFER_MAX_SIZE 88200
#define DELAY_BUFFER_MIN_SIZE 1
#define DELAY_BUFFER_NEUTRAL_SIZE 16384

struct DelayBufferSetup {
	int32_t actualSpinRate;           // 1 is represented as 16777216
	int32_t spinRateForSpedUpWriting; // Normally the same as actualSpinRate, but subject to some limits for safety
	uint32_t divideByRate;            // 1 is represented as 65536
	int32_t rateMultiple;
	uint32_t writeSizeAdjustment;
};

class DelayBuffer {
public:
	DelayBuffer();
	~DelayBuffer();
	Error init(uint32_t newRate, uint32_t failIfThisSize = 0, bool includeExtraSpace = true);
	void makeNativeRatePrecise();
	void makeNativeRatePreciseRelativeToOtherBuffer(DelayBuffer* otherBuffer);
	void discard();
	void setupForRender(int32_t rate, DelayBufferSetup* setup);
	int32_t getIdealBufferSizeFromRate(uint32_t newRate);
	void empty();

	inline bool isActive() { return (bufferStart != NULL); }

	inline bool clearAndMoveOn() {
		bufferCurrentPos->l = 0;
		bufferCurrentPos->r = 0;
		return moveOn();
	}

	inline bool moveOn() {
		bool wrapped = (++bufferCurrentPos == bufferEnd);
		if (wrapped)
			bufferCurrentPos = bufferStart;
		return wrapped;
	}

	inline void writeNative(int32_t toDelayL, int32_t toDelayR) {
		StereoSample* writePos = bufferCurrentPos - delaySpaceBetweenReadAndWrite;
		if (writePos < bufferStart)
			writePos += sizeIncludingExtra;
		writePos->l = toDelayL;
		writePos->r = toDelayR;
	}

	inline void writeNativeAndMoveOn(int32_t toDelayL, int32_t toDelayR, StereoSample** writePos) {
		(*writePos)->l = toDelayL;
		(*writePos)->r = toDelayR;

		(*writePos)++;
		if (*writePos == bufferEnd)
			*writePos = bufferStart;
	}

	[[gnu::always_inline]] inline void writeResampled(int32_t toDelayL, int32_t toDelayR, int32_t strength1,
	                                                  int32_t strength2, DelayBufferSetup* setup) {
		// If delay buffer spinning above sample rate...
		if (setup->actualSpinRate >= 16777216) {

			// An improvement on that could be to only do the triangle-widening when we're down near the native rate -
			// i.e. set a minimum width of double the native rate rather than always doubling the width. The difficulty
			// would be ensuring that we compensate perfectly the strength of each write so that the volume remains the
			// same. It could totally be done. The only real advantage would be that the number of memory writes would
			// be halved at high speeds.

			// For efficiency, we start far-right, then traverse to far-left.
			// I rearranged some algebra to get this from the strengthThisWrite equation
			int32_t howFarRightToStart = (strength2 + (setup->spinRateForSpedUpWriting >> 8)) >> 16;

			// This variable represents one "step" of the delay buffer as 65536.
			// Always positive - absolute distance
			int32_t distanceFromMainWrite = (int32_t)howFarRightToStart << 16;

			// Initially is the far-right right pos, not the central "main" one
			StereoSample* writePos = bufferCurrentPos - delaySpaceBetweenReadAndWrite + howFarRightToStart;
			while (writePos < bufferStart)
				writePos += sizeIncludingExtra;
			while (writePos >= bufferEnd)
				writePos -= sizeIncludingExtra;

			// Do all writes to the right of the main write pos
			while (distanceFromMainWrite != 0) { // For as long as we haven't reached the "main" pos...
				// Check my notebook for a rudimentary diagram
				int32_t strengthThisWrite =
				    (0xFFFFFFFF >> 4) - (((distanceFromMainWrite - strength2) >> 4) * setup->divideByRate);

				writePos->l += multiply_32x32_rshift32(toDelayL, strengthThisWrite) << 3;
				writePos->r += multiply_32x32_rshift32(toDelayR, strengthThisWrite) << 3;

				if (--writePos == bufferStart - 1)
					writePos = bufferEnd - 1;
				// writePos = (writePos == bufferStart) ? bufferEnd - 1 : writePos - 1;
				distanceFromMainWrite -= 65536;
			}

			// Do all writes to the left of (and including) the main write pos
			while (true) {
				int32_t strengthThisWrite =
				    (0xFFFFFFFF >> 4) - (((distanceFromMainWrite + strength2) >> 4) * setup->divideByRate);
				if (strengthThisWrite <= 0)
					break; // And stop when we've got far enough left that we shouldn't be squirting any more juice here

				writePos->l += multiply_32x32_rshift32(toDelayL, strengthThisWrite) << 3;
				writePos->r += multiply_32x32_rshift32(toDelayR, strengthThisWrite) << 3;

				if (--writePos == bufferStart - 1)
					writePos = bufferEnd - 1;
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
			StereoSample* writePos = bufferCurrentPos - delaySpaceBetweenReadAndWrite + 2;

			while (writePos < bufferStart)
				writePos += sizeIncludingExtra;
			// while (writePos >= bufferEnd) writePos -= sizeIncludingExtra; // Not needed - but be careful! Leave this
			// here as a reminder

			int32_t strength[4];

			strength[1] = strength1 + setup->rateMultiple - 65536; // For the "main" pos
			strength[2] = strength2 + setup->rateMultiple - 65536; // For the "main + 1" pos

			// Strengths for the further 1 write in each direction
			strength[0] = strength[1] - 65536;
			strength[3] = strength[2] - 65536;

			int8_t i = 3;
			while (true) {
				if (strength[i] > 0) {
					writePos->l += multiply_32x32_rshift32(toDelayL, (strength[i] >> 2) * setup->writeSizeAdjustment)
					               << 2;
					writePos->r += multiply_32x32_rshift32(toDelayR, (strength[i] >> 2) * setup->writeSizeAdjustment)
					               << 2;
				}
				if (--i < 0)
					break;
				if (--writePos == bufferStart - 1)
					writePos = bufferEnd - 1;
			}
		}
	}

	// For some reason, getting rid of this function and replacing it with the other ones causes actual delays to
	// process like 5% slower...

	inline void write(int32_t toDelayL, int32_t toDelayR, int32_t strength1, int32_t strength2,
	                  DelayBufferSetup* setup) {
		// If no speed adjustment
		if (!isResampling) {
			StereoSample* writePos = bufferCurrentPos - delaySpaceBetweenReadAndWrite;
			if (writePos < bufferStart)
				writePos += sizeIncludingExtra;
			writePos->l = toDelayL;
			writePos->r = toDelayR;
		}

		// If delay buffer spinning above sample rate...
		else if (setup->actualSpinRate >= 16777216) {

			// An improvement on that could be to only do the triangle-widening when we're down near the native rate -
			// i.e. set a minimum width of double the native rate rather than always doubling the width. The difficulty
			// would be ensuring that we compensate perfectly the strength of each write so that the volume remains the
			// same. It could totally be done. The only real advantage would be that the number of memory writes would
			// be halved at high speeds.

			// For efficiency, we start far-right, then traverse to far-left.

			// I rearranged some algebra to get this from the strengthThisWrite equation
			int32_t howFarRightToStart = (strength2 + (setup->spinRateForSpedUpWriting >> 8)) >> 16;

			// This variable represents one "step" of the delay buffer as 65536.
			// Always positive - absolute distance
			int32_t distanceFromMainWrite = (int32_t)howFarRightToStart << 16;

			// Initially is the far-right right pos, not the central "main" one
			StereoSample* writePos = bufferCurrentPos - delaySpaceBetweenReadAndWrite + howFarRightToStart;
			while (writePos < bufferStart)
				writePos += sizeIncludingExtra;
			while (writePos >= bufferEnd)
				writePos -= sizeIncludingExtra;

			// Do all writes to the right of the main write pos
			while (distanceFromMainWrite != 0) { // For as long as we haven't reached the "main" pos...
				// Check my notebook for a rudimentary diagram
				int32_t strengthThisWrite =
				    (0xFFFFFFFF >> 4) - (((distanceFromMainWrite - strength2) >> 4) * setup->divideByRate);

				writePos->l += multiply_32x32_rshift32(toDelayL, strengthThisWrite) << 3;
				writePos->r += multiply_32x32_rshift32(toDelayR, strengthThisWrite) << 3;

				if (--writePos == bufferStart - 1)
					writePos = bufferEnd - 1;
				// writePos = (writePos == bufferStart) ? bufferEnd - 1 : writePos - 1;
				distanceFromMainWrite -= 65536;
			}

			// Do all writes to the left of (and including) the main write pos
			while (true) {
				int32_t strengthThisWrite =
				    (0xFFFFFFFF >> 4) - (((distanceFromMainWrite + strength2) >> 4) * setup->divideByRate);
				if (strengthThisWrite <= 0)
					break; // And stop when we've got far enough left that we shouldn't be squirting any more juice here

				writePos->l += multiply_32x32_rshift32(toDelayL, strengthThisWrite) << 3;
				writePos->r += multiply_32x32_rshift32(toDelayR, strengthThisWrite) << 3;

				if (--writePos == bufferStart - 1)
					writePos = bufferEnd - 1;
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

			StereoSample* writePos = bufferCurrentPos - delaySpaceBetweenReadAndWrite
			                         + 2; // The furthest right we need to write is 2 steps right from the "main" write
			while (writePos < bufferStart)
				writePos += sizeIncludingExtra;
			// while (writePos >= bufferEnd) writePos -= sizeIncludingExtra; // Not needed - but be careful! Leave this
			// here as a reminder

			int32_t strength[4];

			strength[1] = strength1 + setup->rateMultiple - 65536; // For the "main" pos
			strength[2] = strength2 + setup->rateMultiple - 65536; // For the "main + 1" pos

			// Strengths for the further 1 write in each direction
			strength[0] = strength[1] - 65536;
			strength[3] = strength[2] - 65536;

			int8_t i = 3;
			while (true) {
				if (strength[i] > 0) {
					writePos->l += multiply_32x32_rshift32(toDelayL, (strength[i] >> 2) * setup->writeSizeAdjustment)
					               << 2;
					writePos->r += multiply_32x32_rshift32(toDelayR, (strength[i] >> 2) * setup->writeSizeAdjustment)
					               << 2;
				}
				if (--i < 0)
					break;
				if (--writePos == bufferStart - 1)
					writePos = bufferEnd - 1;
			}
		}
	}

	StereoSample* bufferStart;
	StereoSample* bufferEnd;
	StereoSample* bufferCurrentPos;
	uint32_t longPos;
	uint8_t lastShortPos;
	uint32_t nativeRate;
	uint32_t size;
	uint32_t sizeIncludingExtra;
	bool isResampling;
};
