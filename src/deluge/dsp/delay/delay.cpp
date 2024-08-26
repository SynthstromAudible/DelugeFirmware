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

#include "dsp/delay/delay.h"
#include "definitions_cxx.hpp"
#include "dsp/delay/delay_buffer.h"
#include "dsp/stereo_sample.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/sync.h"
#include "processing/engines/audio_engine.h"
#include <cstdlib>
#include <ranges>

extern int32_t spareRenderingBuffer[][SSI_TX_BUFFER_NUM_SAMPLES];

void Delay::informWhetherActive(bool newActive, int32_t userDelayRate) {

	bool previouslyActive = isActive();

	if (previouslyActive != newActive) {
		if (!newActive) {
			discardBuffers();
			return;
		}

setupSecondaryBuffer:
		Error result = secondaryBuffer.init(userDelayRate);
		if (result != Error::NONE) {
			return;
		}
		prepareToBeginWriting();
		postLPFL = 0;
		postLPFR = 0;
		return;
	}

	// Or if no change...

	// If it already was active...
	if (previouslyActive) {
		// If no writing has happened yet to this Delay, check that the buffer is the right size.
		// The delay time might have changed since it was set up, and we could be better off making a new buffer,
		// which is easily done now, before anything's been written
		if (!primaryBuffer.isActive() && secondaryBuffer.isActive()
		    && sizeLeftUntilBufferSwap == getAmountToWriteBeforeReadingBegins()) {

			auto [idealBufferSize, clamped] = DelayBuffer::getIdealBufferSizeFromRate(userDelayRate);

			if (idealBufferSize != secondaryBuffer.size()) {

				D_PRINTLN("new secondary buffer before writing starts");

				// Ditch that secondary buffer, make a new one
				secondaryBuffer.discard();
				goto setupSecondaryBuffer;
			}
		}
	}
}

void Delay::copySecondaryToPrimary() {
	primaryBuffer.discard();
	primaryBuffer = secondaryBuffer; // Does actual copying
	secondaryBuffer.invalidate();    // Make sure this doesn't try to get "deallocated" later
}

void Delay::copyPrimaryToSecondary() {
	secondaryBuffer.discard();
	secondaryBuffer = primaryBuffer; // Does actual copying
	primaryBuffer.invalidate();      // Make sure this doesn't try to get "deallocated" later
}

void Delay::prepareToBeginWriting() {
	sizeLeftUntilBufferSwap = getAmountToWriteBeforeReadingBegins(); // If you change this, make sure you
}

// Set the rate and feedback in the workingState before calling this
void Delay::setupWorkingState(Delay::State& workingState, uint32_t timePerInternalTickInverse, bool anySoundComingIn) {

	// Set some stuff up that we need before we make some decisions
	// BUG: we want to be able to reduce the 256 to 1, but for some reason, the
	// patching engine spits out 112 even when this should be 0...
	bool mightDoDelay = (workingState.delayFeedbackAmount >= 256 && (anySoundComingIn || repeatsUntilAbandon));

	if (mightDoDelay) {

		if (syncLevel != 0) {

			workingState.userDelayRate =
			    multiply_32x32_rshift32_rounded(workingState.userDelayRate, timePerInternalTickInverse);

			// Limit to the biggest number we can store...
			int32_t limit = 2147483647 >> (syncLevel + 5);
			workingState.userDelayRate = std::min(workingState.userDelayRate, limit);
			if (syncType == SYNC_TYPE_EVEN) {} // Do nothing
			else if (syncType == SYNC_TYPE_TRIPLET) {
				workingState.userDelayRate = workingState.userDelayRate * 3 / 2;
			}
			else if (syncType == SYNC_TYPE_DOTTED) {
				workingState.userDelayRate = workingState.userDelayRate * 2 / 3;
			}
			workingState.userDelayRate <<= (syncLevel + 5);
		}
	}

	// Tell it to allocate memory if that hasn't already happened
	informWhetherActive(mightDoDelay, workingState.userDelayRate);
	workingState.doDelay = isActive(); // Check that ram actually is allocated

	if (workingState.doDelay) {
		// If feedback has changed, or sound is coming in, reassess how long to leave the delay sounding for
		if (anySoundComingIn || workingState.delayFeedbackAmount != prevFeedback) {
			setTimeToAbandon(workingState);
			prevFeedback = workingState.delayFeedbackAmount;
		}
	}
}

void Delay::setTimeToAbandon(const Delay::State& workingState) {

	if (!workingState.doDelay) {
		repeatsUntilAbandon = 0;
	}
	else if (workingState.delayFeedbackAmount < 33554432) {
		repeatsUntilAbandon = 1;
	}
	else if (workingState.delayFeedbackAmount <= 100663296) {
		repeatsUntilAbandon = 2;
	}
	else if (workingState.delayFeedbackAmount <= 218103808) {
		repeatsUntilAbandon = 3;
	}
	else if (workingState.delayFeedbackAmount < 318767104) {
		repeatsUntilAbandon = 4;
	}
	else if (workingState.delayFeedbackAmount < 352321536) {
		repeatsUntilAbandon = 5;
	}
	else if (workingState.delayFeedbackAmount < 452984832) {
		repeatsUntilAbandon = 6;
	}
	else if (workingState.delayFeedbackAmount < 520093696) {
		repeatsUntilAbandon = 9;
	}
	else if (workingState.delayFeedbackAmount < 637534208) {
		repeatsUntilAbandon = 12;
	}
	else if (workingState.delayFeedbackAmount < 704643072) {
		repeatsUntilAbandon = 13;
	}
	else if (workingState.delayFeedbackAmount < 771751936) {
		repeatsUntilAbandon = 18;
	}
	else if (workingState.delayFeedbackAmount < 838860800) {
		repeatsUntilAbandon = 24;
	}
	else if (workingState.delayFeedbackAmount < 939524096) {
		repeatsUntilAbandon = 40;
	}
	else if (workingState.delayFeedbackAmount < 1040187392) {
		repeatsUntilAbandon = 110;
	}
	else {
		repeatsUntilAbandon = 255;
	}

	// if (!getRandom255()) D_PRINTLN(workingState.delayFeedbackAmount);
}

void Delay::hasWrapped() {
	if (repeatsUntilAbandon == 255) {
		return;
	}

	repeatsUntilAbandon--;
	if (!repeatsUntilAbandon) {
		// D_PRINTLN("discarding");
		discardBuffers();
	}
}

void Delay::discardBuffers() {
	primaryBuffer.discard();
	secondaryBuffer.discard();
	prevFeedback = 0;
	repeatsUntilAbandon = 0;
}

void Delay::initializeSecondaryBuffer(int32_t newNativeRate, bool makeNativeRatePreciseRelativeToOtherBuffer) {
	Error result = secondaryBuffer.init(newNativeRate, primaryBuffer.size());
	if (result != Error::NONE) {
		return;
	}
	D_PRINTLN("new buffer, size:  %d", secondaryBuffer.size());

	// 2 different options here for different scenarios. I can't very clearly remember how to describe the
	// difference
	if (makeNativeRatePreciseRelativeToOtherBuffer) {
		// D_PRINTLN("making precise");
		primaryBuffer.makeNativeRatePreciseRelativeToOtherBuffer(secondaryBuffer);
	}
	else {
		primaryBuffer.makeNativeRatePrecise();
		secondaryBuffer.makeNativeRatePrecise();
	}
	sizeLeftUntilBufferSwap = secondaryBuffer.size() + 5;
}

void Delay::process(std::span<StereoSample> buffer, const State& delayWorkingState) {
	if (!delayWorkingState.doDelay) {
		return;
	}

	if (delayWorkingState.userDelayRate != userRateLastTime) {
		userRateLastTime = delayWorkingState.userDelayRate;
		countCyclesWithoutChange = 0;
	}
	else {
		countCyclesWithoutChange += buffer.size();
	}

	// If just a single buffer is being used for reading and writing, we can consider making a 2nd buffer
	if (!secondaryBuffer.isActive()) {

		// If resampling previously recorded as happening, or just about to be recorded as happening
		if (primaryBuffer.resampling() || delayWorkingState.userDelayRate != primaryBuffer.nativeRate()) {

			// If delay speed has settled for a split second...
			if (countCyclesWithoutChange >= (kSampleRate >> 5)) {
				// D_PRINTLN("settling");
				initializeSecondaryBuffer(delayWorkingState.userDelayRate, true);
			}

			// If spinning at double native rate, there's no real need to be using such a big buffer, so make a new
			// (smaller) buffer at our new rate
			else if (delayWorkingState.userDelayRate >= (primaryBuffer.nativeRate() << 1)) {
				initializeSecondaryBuffer(delayWorkingState.userDelayRate, false);
			}

			// If spinning below native rate, the quality's going to be suffering, so make a new buffer whose native
			// rate is half our current rate (double the quality)
			else if (delayWorkingState.userDelayRate < primaryBuffer.nativeRate() >> 1) {
				initializeSecondaryBuffer(delayWorkingState.userDelayRate >> 1, false);
			}
		}
	}

	// Figure some stuff out for the primary buffer
	primaryBuffer.setupForRender(delayWorkingState.userDelayRate);

	// Figure some stuff out for the secondary buffer - only if it's active
	if (secondaryBuffer.isActive()) {
		secondaryBuffer.setupForRender(delayWorkingState.userDelayRate);
	}

	bool wrapped = false;

	std::span<StereoSample> working_buffer{reinterpret_cast<StereoSample*>(spareRenderingBuffer[0]), buffer.size()};

	GeneralMemoryAllocator::get().checkStack("delay");

	StereoSample* primaryBufferOldPos;
	uint32_t primaryBufferOldLongPos;
	uint8_t primaryBufferOldLastShortPos;

	// If nothing to read yet, easy
	if (!primaryBuffer.isActive()) {
		std::fill(working_buffer.begin(), working_buffer.end(), StereoSample{0, 0});
	}

	// Or...
	else {

		primaryBufferOldPos = &primaryBuffer.current();
		primaryBufferOldLongPos = primaryBuffer.longPos;
		primaryBufferOldLastShortPos = primaryBuffer.lastShortPos;

		// Native read
		if (primaryBuffer.isNative()) {
			for (StereoSample& sample : working_buffer) {
				wrapped = primaryBuffer.clearAndMoveOn() || wrapped;
				sample = primaryBuffer.current();
			}
		}

		// Or, resampling read
		else {

			for (StereoSample& sample : working_buffer) {
				// Move forward, and clear buffer as we go
				int32_t primaryStrength2 = primaryBuffer.advance([&] {
					wrapped = primaryBuffer.clearAndMoveOn() || wrapped; //<
				});

				int32_t primaryStrength1 = 65536 - primaryStrength2;

				StereoSample* nextPos = &primaryBuffer.current() + 1;
				if (nextPos == primaryBuffer.end()) {
					nextPos = primaryBuffer.begin();
				}
				StereoSample fromDelay1 = primaryBuffer.current();
				StereoSample fromDelay2 = *nextPos;

				sample.l = (multiply_32x32_rshift32(fromDelay1.l, primaryStrength1 << 14)
				            + multiply_32x32_rshift32(fromDelay2.l, primaryStrength2 << 14))
				           << 2;
				sample.r = (multiply_32x32_rshift32(fromDelay1.r, primaryStrength1 << 14)
				            + multiply_32x32_rshift32(fromDelay2.r, primaryStrength2 << 14))
				           << 2;
			}
		}
	}

	if (analog) {

		for (StereoSample& sample : working_buffer) {
			ir_processor.process(sample, sample);
		}

		for (StereoSample& sample : working_buffer) {
			// impulseResponseProcessor.process(sample, sample);

			// Reduce headroom, since this sounds ok with analog sim
			sample.l = getTanHUnknown(multiply_32x32_rshift32(sample.l, delayWorkingState.delayFeedbackAmount),
			                          delayWorkingState.analog_saturation)
			           << 2;
			sample.r = getTanHUnknown(multiply_32x32_rshift32(sample.r, delayWorkingState.delayFeedbackAmount),
			                          delayWorkingState.analog_saturation)
			           << 2;
		}
	}

	else {
		for (StereoSample& sample : working_buffer) {
			// Leave more headroom, because making it clip sounds bad with pure digital
			sample.l = signed_saturate<32 - 3>(multiply_32x32_rshift32(sample.l, delayWorkingState.delayFeedbackAmount))
			           << 2;
			sample.r = signed_saturate<32 - 3>(multiply_32x32_rshift32(sample.r, delayWorkingState.delayFeedbackAmount))
			           << 2;
		}
	}

	// HPF on delay output, to stop it "farting out". Corner frequency is somewhere around 40Hz after many
	// repetitions
	for (StereoSample& sample : working_buffer) {
		int32_t distanceToGoL = sample.l - postLPFL;
		postLPFL += distanceToGoL >> 11;
		sample.l -= postLPFL;

		int32_t distanceToGoR = sample.r - postLPFR;
		postLPFR += distanceToGoR >> 11;
		sample.r -= postLPFR;
	}

	// Go through what we grabbed, sending it to the audio output buffer, and also preparing it to be fed back
	// into the delay
	for (auto [input, output] : std::views::zip(working_buffer, buffer)) {
		StereoSample current = input;

		// Feedback calculation, and combination with input
		if (pingPong && AudioEngine::renderInStereo) {
			input.l = current.r;
			input.r = ((output.l + output.r) >> 1) + current.l;
		}
		else {
			input += output;
		}

		// Output
		output += current;
	}

	// And actually feedback being applied back into the actual delay primary buffer...
	if (primaryBuffer.isActive()) {

		// Native
		if (primaryBuffer.isNative()) {
			StereoSample* writePos = primaryBufferOldPos - delaySpaceBetweenReadAndWrite;
			if (writePos < primaryBuffer.begin()) {
				writePos += primaryBuffer.sizeIncludingExtra;
			}

			for (StereoSample sample : working_buffer) {
				primaryBuffer.writeNativeAndMoveOn(sample, &writePos);
			}
		}

		// Resampling
		else {
			primaryBuffer.setCurrent(primaryBufferOldPos);
			primaryBuffer.longPos = primaryBufferOldLongPos;
			primaryBuffer.lastShortPos = primaryBufferOldLastShortPos;

			for (StereoSample sample : working_buffer) {
				// Move forward
				int32_t primaryStrength2 = primaryBuffer.advance([&] {
					primaryBuffer.moveOn(); //<
				});
				int32_t primaryStrength1 = 65536 - primaryStrength2;

				primaryBuffer.writeResampled(sample, primaryStrength1, primaryStrength2);
			}
		}
	}

	// And secondary buffer
	// If secondary buffer active, we need to tick it along and write to it too
	if (secondaryBuffer.isActive()) {

		// We want to disregard whatever the primary buffer told us, and just use the secondary one now
		wrapped = false;

		// Native
		if (secondaryBuffer.isNative()) {
			for (StereoSample sample : working_buffer) {
				wrapped = secondaryBuffer.clearAndMoveOn() || wrapped;
				sizeLeftUntilBufferSwap--;

				// Write to secondary buffer
				secondaryBuffer.writeNative(sample);
			};
		}

		// Resampled
		else {

			for (StereoSample sample : working_buffer) {
				// Move forward, and clear buffer as we go
				int32_t secondaryStrength2 = secondaryBuffer.advance([&] {
					wrapped = secondaryBuffer.clearAndMoveOn() || wrapped; //<
					sizeLeftUntilBufferSwap--;
				});

				int32_t secondaryStrength1 = 65536 - secondaryStrength2;

				// Write to secondary buffer
				secondaryBuffer.writeResampled(sample, secondaryStrength1, secondaryStrength2);
			}
		}

		if (sizeLeftUntilBufferSwap < 0) {
			copySecondaryToPrimary();
		}
	}

	if (wrapped) {
		hasWrapped();
	}
}
