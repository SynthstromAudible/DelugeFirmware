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

#include "Delay.h"
#include <stdlib.h>
#include "AudioSample.h"
#include "definitions.h"
//#include <algorithm>
#include "uart.h"
#include "playbackhandler.h"
#include "functions.h"
#include "song.h"
#include "FlashStorage.h"

Delay::Delay() {
	pingPong = true;
	analog = false;
	repeatsUntilAbandon = 0;
	prevFeedback = 0;

	// I'm so sorry, this is incredibly ugly, but in order to decide the default sync level, we have to look at the current song, or even better the one being preloaded.
	Song* song = preLoadedSong;
	if (!song) song = currentSong;
	if (song) {
		sync = 8 - (song->insideWorldTickMagnitude + song->insideWorldTickMagnitudeOffsetFromBPM);
	}
	else {
		sync = 8 - FlashStorage::defaultMagnitude;
	}
}

void Delay::cloneFrom(Delay* other) {
	pingPong = other->pingPong;
	analog = other->analog;
	sync = other->sync;
}

void Delay::informWhetherActive(bool newActive, int32_t userDelayRate) {

	bool previouslyActive = isActive();

	if (previouslyActive != newActive) {
		if (newActive) {
setupSecondaryBuffer:
			uint8_t result = secondaryBuffer.init(userDelayRate);
			if (result != NO_ERROR) return;
			prepareToBeginWriting();
			postLPFL = 0;
			postLPFR = 0;
		}
		else {
			discardBuffers();
		}
	}

	// Or if no change...
	else {

		// If it already was active...
		if (previouslyActive) {
			// If no writing has happened yet to this Delay, check that the buffer is the right size.
			// The delay time might have changed since it was set up, and we could be better off making a new buffer, which is easily done now, before anything's been written
			if (!primaryBuffer.isActive() && secondaryBuffer.isActive()
			    && sizeLeftUntilBufferSwap == getAmountToWriteBeforeReadingBegins()) {

				int32_t idealBufferSize = secondaryBuffer.getIdealBufferSizeFromRate(userDelayRate);
				idealBufferSize = getMin(idealBufferSize, (int32_t)DELAY_BUFFER_MAX_SIZE);
				idealBufferSize = getMax(idealBufferSize, (int32_t)DELAY_BUFFER_MIN_SIZE);

				if (idealBufferSize != secondaryBuffer.size) {

					Uart::println("new secondary buffer before writing starts");

					// Ditch that secondary buffer, make a new one
					secondaryBuffer.discard();
					goto setupSecondaryBuffer;
				}
			}
		}
	}
}

void Delay::copySecondaryToPrimary() {
	primaryBuffer.discard();
	primaryBuffer = secondaryBuffer;    // Does actual copying
	secondaryBuffer.bufferStart = NULL; // Make sure this doesn't try to get "deallocated" later
}

void Delay::copyPrimaryToSecondary() {
	secondaryBuffer.discard();
	secondaryBuffer = primaryBuffer;  // Does actual copying
	primaryBuffer.bufferStart = NULL; // Make sure this doesn't try to get "deallocated" later
}

void Delay::prepareToBeginWriting() {
	sizeLeftUntilBufferSwap = getAmountToWriteBeforeReadingBegins(); // If you change this, make sure you
}

int Delay::getAmountToWriteBeforeReadingBegins() {
	return secondaryBuffer.size;
}

bool Delay::isActive() {
	return (primaryBuffer.isActive() || secondaryBuffer.isActive());
}

// Set the rate and feedback in the workingState before calling this
void Delay::setupWorkingState(DelayWorkingState* workingState, bool anySoundComingIn) {

	// Set some stuff up that we need before we make some decisions
	bool mightDoDelay =
	    (workingState->delayFeedbackAmount >= 256
	     && (anySoundComingIn
	         || repeatsUntilAbandon)); // TODO: we want to be able to reduce the 256 to 1, but for some reason, the patching engine spits out 112 even when this should be 0...

	if (mightDoDelay) {

		if (sync != 0) {

			workingState->userDelayRate = multiply_32x32_rshift32_rounded(
			    workingState->userDelayRate, playbackHandler.getTimePerInternalTickInverse(true));

			// Limit to the biggest number we can store...
			int32_t limit = 2147483647 >> (sync + 5);
			workingState->userDelayRate = getMin(workingState->userDelayRate, limit);
			workingState->userDelayRate <<= (sync + 5);
		}
	}

	informWhetherActive(mightDoDelay,
	                    workingState->userDelayRate); // Tell it to allocate memory if that hasn't already happened
	workingState->doDelay = isActive();               // Check that ram actually is allocated

	if (workingState->doDelay) {
		// If feedback has changed, or sound is coming in, reassess how long to leave the delay sounding for
		if (anySoundComingIn || workingState->delayFeedbackAmount != prevFeedback) {
			setTimeToAbandon(workingState);
			prevFeedback = workingState->delayFeedbackAmount;
		}
	}
}

void Delay::setTimeToAbandon(DelayWorkingState* workingState) {

	if (!workingState->doDelay) repeatsUntilAbandon = 0;

	else if (workingState->delayFeedbackAmount < 33554432) repeatsUntilAbandon = 1;
	else if (workingState->delayFeedbackAmount <= 100663296) repeatsUntilAbandon = 2;
	else if (workingState->delayFeedbackAmount <= 218103808) repeatsUntilAbandon = 3;
	else if (workingState->delayFeedbackAmount < 318767104) repeatsUntilAbandon = 4;
	else if (workingState->delayFeedbackAmount < 352321536) repeatsUntilAbandon = 5;
	else if (workingState->delayFeedbackAmount < 452984832) repeatsUntilAbandon = 6;
	else if (workingState->delayFeedbackAmount < 520093696) repeatsUntilAbandon = 9;
	else if (workingState->delayFeedbackAmount < 637534208) repeatsUntilAbandon = 12;
	else if (workingState->delayFeedbackAmount < 704643072) repeatsUntilAbandon = 13;
	else if (workingState->delayFeedbackAmount < 771751936) repeatsUntilAbandon = 18;
	else if (workingState->delayFeedbackAmount < 838860800) repeatsUntilAbandon = 24;
	else if (workingState->delayFeedbackAmount < 939524096) repeatsUntilAbandon = 40;
	else if (workingState->delayFeedbackAmount < 1040187392) repeatsUntilAbandon = 110;
	else repeatsUntilAbandon = 255;

	//if (!getRandom255()) Uart::println(workingState->delayFeedbackAmount);
}

void Delay::hasWrapped() {
	if (repeatsUntilAbandon == 255) return;

	repeatsUntilAbandon--;
	if (!repeatsUntilAbandon) {
		//Uart::println("discarding");
		discardBuffers();
	}
}

void Delay::discardBuffers() {
	primaryBuffer.discard();
	secondaryBuffer.discard();
	prevFeedback = 0;
	repeatsUntilAbandon = 0;
}
