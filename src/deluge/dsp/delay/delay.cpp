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
#include "io/debug/log.h"
#include <cstdlib>

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

			int32_t idealBufferSize = DelayBuffer::getIdealBufferSizeFromRate(userDelayRate);
			idealBufferSize = std::min(idealBufferSize, (int32_t)DelayBuffer::kMaxSize);
			idealBufferSize = std::max(idealBufferSize, (int32_t)DelayBuffer::kMinSize);

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
