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

#include "dsp/convolution/impulse_response_processor.h"
#include "dsp/delay/delay_buffer.h"
#include "dsp/stereo_sample.h"
#include <cstdint>
#include <math.h>

struct DelayWorkingState {
	bool doDelay;
	int32_t userDelayRate;
	int32_t delayFeedbackAmount;
};

class Delay {
public:
	Delay();
	void cloneFrom(Delay* other);
	bool isActive();
	void informWhetherActive(bool newActive, int32_t userDelayRate = 0);
	void copySecondaryToPrimary();
	void copyPrimaryToSecondary();
	void setupWorkingState(DelayWorkingState* workingState, uint32_t timePerInternalTickInverse,
	                       bool anySoundComingIn = true);
	void discardBuffers();
	void setTimeToAbandon(DelayWorkingState* workingState);
	void hasWrapped();

	DelayBuffer primaryBuffer;
	DelayBuffer secondaryBuffer;
	ImpulseResponseProcessor impulseResponseProcessor;

	uint32_t countCyclesWithoutChange;
	int32_t userRateLastTime;
	bool pingPong;
	bool analog;
	SyncType syncType;
	SyncLevel syncLevel; // Basically, 0 is off, max value is 9. Higher numbers are shorter intervals (higher speed).
	int32_t sizeLeftUntilBufferSwap;

	int32_t postLPFL;
	int32_t postLPFR;

	int32_t prevFeedback;

	uint8_t repeatsUntilAbandon; // 0 means never abandon

private:
	void prepareToBeginWriting();
	int32_t getAmountToWriteBeforeReadingBegins();
};
