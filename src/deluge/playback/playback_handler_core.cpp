/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "playback/playback_handler.h"

#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "processing/engines/audio_engine.h"

PlaybackHandler playbackHandler{};

PlaybackHandler::PlaybackHandler() {
	tapTempoNumPresses = 0;
	playbackState = 0;
	analogInTicksPPQN = 24;
	analogOutTicksPPQN = 24;
	analogClockInputAutoStart = true;
	metronomeOn = false;
	midiOutClockEnabled = true;
	midiInClockEnabled = true;
	tempoMagnitudeMatchingEnabled = false;
	posToNextContinuePlaybackFrom = 0;
	stopOutputRecordingAtLoopEnd = false;
	recording = RECORDING_OFF;
	countInEnabled = true;
	timeLastMIDIStartOrContinueMessageSent = 0;
	currentVisualCountForCountIn = 0;
}

float PlaybackHandler::calculateBPM(float timePerInternalTick) {
	float timePerTimerTick = timePerInternalTick;
	if (currentSong->insideWorldTickMagnitude > 0) {
		timePerTimerTick *= ((uint32_t)1 << (currentSong->insideWorldTickMagnitude));
	}
	float tempoBPM = (float)110250 / timePerTimerTick;
	if (currentSong->insideWorldTickMagnitude < 0) {
		tempoBPM *= ((uint32_t)1 << (-currentSong->insideWorldTickMagnitude));
	}
	return tempoBPM;
}

bool PlaybackHandler::currentlySendingMIDIOutputClocks() {
	return midiOutClockEnabled;
}

int32_t PlaybackHandler::getActualArrangementRecordPos() {
	return getActualSwungTickCount() + arrangement.playbackStartedAtPos;
}

int32_t PlaybackHandler::getArrangementRecordPosAtLastActionedSwungTick() {
	return lastSwungTickActioned + arrangement.playbackStartedAtPos;
}

uint32_t PlaybackHandler::getTimePerInternalTick() {
	if (playbackState & PLAYBACK_CLOCK_EXTERNAL_ACTIVE) {
		return timePerInternalTickMovingAverage; // lowpassedTimePerInternalTick
	}
	else {
		return currentSong->getTimePerTimerTickRounded();
	}
}

uint64_t PlaybackHandler::getTimePerInternalTickBig() {
	if (playbackState & PLAYBACK_CLOCK_EXTERNAL_ACTIVE) {
		return (uint64_t)timePerInternalTickMovingAverage << 32;
	}
	else {
		return currentSong->timePerTimerTickBig;
	}
}

float PlaybackHandler::getTimePerInternalTickFloat() {
	if (playbackState & PLAYBACK_CLOCK_EXTERNAL_ACTIVE) {
		return timePerInternalTickMovingAverage;
	}
	else {
		return currentSong->getTimePerTimerTickFloat();
	}
}

uint32_t PlaybackHandler::getTimePerInternalTickInverse(bool getStickyValue) {
	if (playbackState & PLAYBACK_CLOCK_EXTERNAL_ACTIVE) {
		if (getStickyValue) {
			return stickyCurrentTimePerInternalTickInverse;
		}
		else {
			return veryCurrentTimePerInternalTickInverse;
		}
	}
	else {
		return currentSong->divideByTimePerTimerTick;
	}
}

int64_t PlaybackHandler::getActualSwungTickCount(uint32_t* timeRemainder) {

	int64_t actualSwungTick;

	// Internal clock
	if (playbackState & PLAYBACK_CLOCK_INTERNAL_ACTIVE) {

		// If first timer tick not actioned yet (not sure if we ever get called in this case, though)...
		if (!nextTimerTickScheduled) {
			if (timeRemainder) {
				*timeRemainder = 0;
			}
			return 0;
		}

		actualSwungTick = lastTimerTickActioned + getNumSwungTicksInSinceLastTimerTick(timeRemainder);
	}

	// External clock
	else {

		if (timeRemainder) {
			*timeRemainder = 0; // TODO: fix this!
		}

		float currentInternalTick = getCurrentInternalTickFloatFollowingExternalClock();

		// No swing
		if (!currentSong->hasAnySwing()) {
			actualSwungTick = currentInternalTick;
		}

		// Yes swing
		else {

			int32_t leftShift = 9 - currentSong->swingInterval;
			leftShift = std::max(leftShift, 0_i32);
			uint32_t swingInterval = 3 << (leftShift);
			uint32_t doubleSwingInterval = swingInterval << 1;

			uint64_t startOfSwingBlock = (uint64_t)currentInternalTick / doubleSwingInterval * doubleSwingInterval;
			float posWithinSwingBlock = currentInternalTick - startOfSwingBlock;

			// First, see if we're in the first half still
			float swungTicksIn = (uint32_t)(posWithinSwingBlock * 50) / (uint32_t)(50 + currentSong->swingAmount);
			if (swungTicksIn < swingInterval) {
				actualSwungTick = startOfSwingBlock + swungTicksIn;
			}

			// Or, if we're in the second half
			else {
				float posTilEndOfSwingBlock = (float)doubleSwingInterval - posWithinSwingBlock;
				float swungTicksTilEnd =
				    (uint32_t)(posTilEndOfSwingBlock * 50) / (uint32_t)(50 - currentSong->swingAmount);
				actualSwungTick = startOfSwingBlock + doubleSwingInterval - (swungTicksTilEnd + 1); // Round that bit up
			}
		}
	}

	// Make sure the result isn't outside of its possible range

	if (actualSwungTick < lastSwungTickActioned) {
		actualSwungTick = lastSwungTickActioned;
		if (timeRemainder) {
			*timeRemainder = 0;
		}
	}

	else {
		int64_t nextSwungTickToAction = lastSwungTickActioned + swungTicksTilNextEvent;
		if (nextSwungTickToAction // Checking for special case when nextSwungTickToAction == 0, when playback first starts.
		    // Unchecked, that would sometimes lead to us returning -1 when following external clock.
		    && actualSwungTick >= nextSwungTickToAction) {
			actualSwungTick = nextSwungTickToAction - 1;
			if (timeRemainder) {
				*timeRemainder = getTimePerInternalTick() - 1; // A bit cheesy...
			}
		}
	}

	return actualSwungTick;
}

int64_t PlaybackHandler::getCurrentInternalTickCount(uint32_t* timeRemainder) {

	uint32_t timePerTimerTick = currentSong->getTimePerTimerTickRounded();

	int64_t internalTickCount;

	// Internal clock
	if (playbackState & PLAYBACK_CLOCK_INTERNAL_ACTIVE) {

		// If no timer ticks have occurred yet, the answer is a resounding zero, and we have to have this as a special case because timeLastTimerTickBig
		// won't have been set yet. This will happen all the time during playback setup, e.g. in AudioClip::resumePlayback()
		if (!nextTimerTickScheduled) {
			if (timeRemainder) {
				*timeRemainder = 0;
			}
			internalTickCount = 0;
		}

		// Or, the normal case - calculate the answer
		else {
			uint32_t timeSinceLastTimerTick = AudioEngine::audioSampleTimer - (uint32_t)(timeLastTimerTickBig >> 32);
			int32_t ticksSinceLastTimerTick = timeSinceLastTimerTick / timePerTimerTick;
			if (timeRemainder) {
				*timeRemainder = timeSinceLastTimerTick - (ticksSinceLastTimerTick * timePerTimerTick);
			}
			internalTickCount = lastTimerTickActioned + ticksSinceLastTimerTick;

			// Safety against rounding errors - make sure we don't give an internal tick count that's less than a swung tick we already actioned.
			// I've seen this happen (or at least become apparent) when at 6144 resolution with very shortened audio clip.
			if (internalTickCount < lastSwungTickActioned) {
				internalTickCount = lastSwungTickActioned;
			}
		}
	}

	// External clock
	else {

		if (timeRemainder) {
			*timeRemainder = 0; // TODO: fix this!
		}

		internalTickCount = getCurrentInternalTickFloatFollowingExternalClock();
	}

#if ALPHA_OR_BETA_VERSION
	if (internalTickCount < 0) {
		// Trying to narrow down "nofg" error, which Ron got most recently (Nov 2021). Wait no, he didn't have playback on!
		FREEZE_WITH_ERROR("E429");
	}
#endif

	return internalTickCount;
}

float PlaybackHandler::getCurrentInternalTickFloatFollowingExternalClock() {

	// If we've only actually received one (or none - is that possible?) input tick...
	if (lastInputTickReceived <= 0) {
		// We're before it, which won't make sense to the caller, so just say we're at 0
		return 0;
	}

	int32_t t = 0;

	int32_t timeSinceLastInputTick = AudioEngine::audioSampleTimer - timeLastInputTicks[t];

	float currentInputTick;

	// If that input tick hasn't "happened" yet and is currently just scheduled to happen soon, then the current internal tick is before it
	if (timeSinceLastInputTick < 0) {

goAgain:
		int32_t timeSincePreviousInputTick = AudioEngine::audioSampleTimer - timeLastInputTicks[t + 1];

		// If the previous one also hasn't happened yet, look a further one back
		if (timeSincePreviousInputTick < 0) {
			timeSinceLastInputTick = timeSincePreviousInputTick;
			t++;
			if (t >= lastInputTickReceived) {
				return 0; // If all the input ticks received so far have not yet "happened"
			}
			if (t >= NUM_INPUT_TICKS_FOR_MOVING_AVERAGE
			             - 1) { // If we just didn't remember that far back - should never really happen
				currentInputTick =
				    lastInputTickReceived - NUM_INPUT_TICKS_FOR_MOVING_AVERAGE; // Gonna be inexact, sorry!
				goto gotCurrentInputTick;
			}
			goto goAgain;
		}

		// Just see how far apart the last two received input ticks were (even though we haven't actually actioned the most recent one yet)
		int32_t timeBetweenInputTicks = timeLastInputTicks[t] - timeLastInputTicks[t + 1];

		// Should now be impossible for them to be at the same time, since we should be looking at one in the future and one not
		if (ALPHA_OR_BETA_VERSION && timeBetweenInputTicks <= 0) {
			FREEZE_WITH_ERROR("E337");
		}

		currentInputTick =
		    (float)timeSincePreviousInputTick / (uint32_t)timeBetweenInputTicks + lastInputTickReceived - t - 1;
	}

	// Or if it has happened...
	else {
		if (timeSinceLastInputTick >= timePerInputTickMovingAverage) {
			timeSinceLastInputTick = timePerInputTickMovingAverage - 1;
		}
		currentInputTick = (float)timeSinceLastInputTick / timePerInputTickMovingAverage + lastInputTickReceived;
	}

gotCurrentInputTick:
	uint32_t internalTicksPer;
	uint32_t inputTicksPer;
	getInternalTicksToInputTicksRatio(&inputTicksPer, &internalTicksPer);

	float currentInternalTick = currentInputTick / inputTicksPer * internalTicksPer;

	return currentInternalTick;
}

int32_t PlaybackHandler::getInternalTickTime(int64_t internalTickCount) {

	// Internal clock
	if (playbackState & PLAYBACK_CLOCK_INTERNAL_ACTIVE) {

		// If first timer tick hasn't even occurred yet, various values will not yet be valid. The time of the first tick will not even have been decided.
		// So, use audioDriver.audioSampleTimer in its place, since that is what our returned value will be compared to.
		if (!nextTimerTickScheduled) {
			return AudioEngine::audioSampleTimer
			       + (((int64_t)currentSong->timePerTimerTickBig * internalTickCount) >> 32);
		}

		int32_t numTicksAfterLastTimerTick = internalTickCount - lastTimerTickActioned; // Could be negative
		return (int64_t)(timeLastTimerTickBig + (int64_t)currentSong->timePerTimerTickBig * numTicksAfterLastTimerTick)
		       >> 32;
	}

	// External clock
	else {
		uint32_t internalTicksPer;
		uint32_t inputTicksPer;
		getInternalTicksToInputTicksRatio(&inputTicksPer, &internalTicksPer);

		float inputTickCount = (float)(internalTickCount * inputTicksPer) / internalTicksPer;

		return (int32_t)timeLastInputTicks[0]
		       + (int32_t)((inputTickCount - lastInputTickReceived) * (int32_t)timePerInputTickMovingAverage);
	}
}

void PlaybackHandler::getInternalTicksToInputTicksRatio(uint32_t* inputTicksPer, uint32_t* internalTicksPer) {

	int16_t inputTickMagnitude;
	uint32_t inputTickScale;

	inputTickMagnitude = currentSong->insideWorldTickMagnitude;
	inputTickScale = currentSong->getInputTickScale();

	if (usingAnalogClockInput) {
		*inputTicksPer = analogInTicksPPQN;
		*internalTicksPer = 8; // Will usually get multiplied by 3 ( *= inputTickScale below ) to make 24
	}
	else {
		*inputTicksPer = 3;
		*internalTicksPer = 1; // Will usually get multiplied by 3 ( *= inputTickScale below ) to make 3
	}

	*internalTicksPer *= inputTickScale;

	if (inputTickMagnitude >= 0) {
		*internalTicksPer <<= inputTickMagnitude;
	}
	else {
		*inputTicksPer <<= (0 - inputTickMagnitude);
	}
}

int32_t PlaybackHandler::getNumSwungTicksInSinceLastActionedSwungTick(uint32_t* timeRemainder) {
	if (currentlyActioningSwungTickOrResettingPlayPos) {
		if (timeRemainder) {
			*timeRemainder = 0;
		}
		return 0; // This will save some time, even though it was already returning the correct result.
	}

	return getActualSwungTickCount(timeRemainder) - lastSwungTickActioned;
}

// This just uses the rounded timePerTimerTick. Should be adequate
int32_t PlaybackHandler::getNumSwungTicksInSinceLastTimerTick(uint32_t* timeRemainder) {
	// If first timer tick not actioned yet (currently the only function that calls this function already separately deals with this though)...
	if (!nextTimerTickScheduled) {
		if (timeRemainder) {
			*timeRemainder = 0;
		}
		return 0;
	}

	uint32_t timePerTimerTick = currentSong->getTimePerTimerTickRounded();

	uint32_t timePassed = AudioEngine::audioSampleTimer - (uint32_t)(timeLastTimerTickBig >> 32);

	if (currentSong->hasAnySwing()) {

		if (timeRemainder) {
			*timeRemainder = 0; // To be improved
		}

		int32_t leftShift = 9 - currentSong->swingInterval;
		leftShift = std::max(leftShift, 0_i32);
		uint32_t swingInterval = 3 << (leftShift);

		// First, see if we're in the first half still
		uint32_t timePassedFiddledWith = (uint32_t)(timePassed * 50) / (uint32_t)(50 + currentSong->swingAmount);
		uint32_t ticksIn = timePassedFiddledWith / timePerTimerTick;
		if (ticksIn < swingInterval) {
			return ticksIn;
		}

		// Or if we're still here, it's in the second half
		uint32_t timeTilNextTimerTick = (uint32_t)(timeNextTimerTickBig >> 32) - AudioEngine::audioSampleTimer;
		uint32_t timeTilNextFiddledWith =
		    (uint32_t)(timeTilNextTimerTick * 50) / (uint32_t)(50 - currentSong->swingAmount);
		if (!timeTilNextFiddledWith) {
			return 1; // Otherwise we'd get a negative number when subtracting 1 below
		}
		int32_t ticksTilEnd = (uint32_t)(timeTilNextFiddledWith - 1) / (uint32_t)timePerTimerTick + 1; // Rounds up.
		return (swingInterval << 1) - ticksTilEnd;
	}

	else {
		int32_t numSwungTicks = timePassed / timePerTimerTick;
		if (timeRemainder) {
			*timeRemainder = timePassed - numSwungTicks * timePerTimerTick;
		}
		return numSwungTicks;
	}
}

bool PlaybackHandler::isCurrentlyRecording() {
	return (playbackState && recording);
}

// Called when playing synced and sync scaling or magnitude have been changed - e.g. when user doubles or halves tempo, or sync scaling is activated
void PlaybackHandler::resyncInternalTicksToInputTicks(Song* song) {
	if (playbackState & PLAYBACK_CLOCK_EXTERNAL_ACTIVE) {
		// This works. Although it doesn't do anything special to account for swing, no long-term out-of-sync-ness
		// results - I tested.
		lastSwungTickActioned = getCurrentInternalTickFloatFollowingExternalClock();
		currentPlaybackMode->resyncToSongTicks(song);

		// In most cases, if we're here, we'll want to alter the "following" internal tick tempo to "remember" what time-scaling stuff we changed.
		// This happens routinely every ~24 clocks anyway, but sometimes it makes sense to store this change instantly, e.g. if we just changed time-scaling like we probably just did
		resetTimePerInternalTickMovingAverage();
	}
}

void PlaybackHandler::resetTimePerInternalTickMovingAverage() {
	// Only do this if no tempo-targeting (that'd be a disaster!!), and if some input ticks have actually been received
	if (!tempoMagnitudeMatchingActiveNow && lastInputTickReceived > 0) {

		uint32_t internalTicksPer;
		uint32_t inputTicksPer;
		getInternalTicksToInputTicksRatio(&inputTicksPer, &internalTicksPer);

		timePerInternalTickMovingAverage = timePerInputTickMovingAverage * inputTicksPer / internalTicksPer;
	}
}
