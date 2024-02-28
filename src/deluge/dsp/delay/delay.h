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
#include "dsp/convolution/impulse_response_processor.h"
#include "dsp/delay/delay_buffer.h"
#include <cstdint>
#include <span>

class Delay {
public:
	struct State {
		bool doDelay;
		int32_t userDelayRate;
		int32_t delayFeedbackAmount;
	};

	Delay() = default;
	Delay(const Delay& other) = delete;

	// was originally cloneFrom
	Delay& operator=(const Delay& rhs) {
		pingPong = rhs.pingPong;
		analog = rhs.analog;
		syncLevel = rhs.syncLevel;
		return *this;
	}

	[[nodiscard]] constexpr bool isActive() const { return (primaryBuffer.isActive() || secondaryBuffer.isActive()); }

	void informWhetherActive(bool newActive, int32_t userDelayRate = 0);
	void copySecondaryToPrimary();
	void copyPrimaryToSecondary();
	void initializeSecondaryBuffer(int32_t newNativeRate, bool makeNativeRatePreciseRelativeToOtherBuffer);
	void setupWorkingState(State& workingState, uint32_t timePerInternalTickInverse, bool anySoundComingIn = true);
	void discardBuffers();
	void setTimeToAbandon(const State& workingState);
	void hasWrapped();

	DelayBuffer primaryBuffer;
	DelayBuffer secondaryBuffer;
	ImpulseResponseProcessor ir_processor;

	uint32_t countCyclesWithoutChange;
	int32_t userRateLastTime;
	bool pingPong = true;
	bool analog = false;

	SyncType syncType = SYNC_TYPE_EVEN;

	// Basically, 0 is off, max value is 9. Higher numbers are shorter intervals (higher speed).
	SyncLevel syncLevel = SYNC_LEVEL_16TH;

	int32_t sizeLeftUntilBufferSwap;

	int32_t postLPFL;
	int32_t postLPFR;

	int32_t prevFeedback = 0;

	uint8_t repeatsUntilAbandon = 0; // 0 means never abandon

	void process(std::span<StereoSample> buffer, State delayWorkingState, int32_t analogDelaySaturationAmount);

private:
	void prepareToBeginWriting();
	[[nodiscard]] constexpr int32_t getAmountToWriteBeforeReadingBegins() const { return secondaryBuffer.size(); }
};
