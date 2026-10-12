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
#include "dsp-next/fixed/delay/delay_processor.h"
#include "dsp/delay/delay_buffer.h"
#include "engines/effects/state.h"
#include "modulation/sync.h"

#include <cstdint>
#include <span>

/// @return How many blocks an OFF delay has been asked to sound: feedback enough to start, with sound coming in.
uint32_t delayOffSilencedBlocks();

class Delay {
public:
	/// The block's working state: the rate and feedback its owner derived, and the owner's settings it plays with.
	struct State {
		bool doDelay{};
		int32_t userDelayRate{};
		int32_t delayFeedbackAmount{};
		int32_t analog_saturation = 8;
		bool ping_pong{true};
		bool analog{false};
	};

	Delay() = default;
	Delay(const Delay& other) = delete;
	Delay& operator=(const Delay& other) = delete;

	[[nodiscard]] constexpr bool isActive() const {
		return (primaryBuffer.tape().isActive() || secondaryBuffer.tape().isActive());
	}

	void informWhetherActive(bool newActive, int32_t userDelayRate = 0);
	void copySecondaryToPrimary();
	void copyPrimaryToSecondary();
	void initializeSecondaryBuffer(int32_t newNativeRate, bool makeNativeRatePreciseRelativeToOtherBuffer);
	/// Fill @p workingState's rate (synced to @p settings' level and type when it is synced) and settings for a block.
	void setupWorkingState(State& workingState, const deluge::effects::DelayState& settings,
	                       uint32_t timePerInternalTickInverse, bool anySoundComingIn = true);
	void discardBuffers();
	void setTimeToAbandon(const State& workingState);
	void hasWrapped();

	DelayBuffer primaryBuffer;
	DelayBuffer secondaryBuffer;
	/// The block's DSP: the read, the feedback's shaping and high-pass, the mix and the writes.
	deluge::dsp::fixed::DelayProcessor processor;

	uint32_t countCyclesWithoutChange{};
	int32_t userRateLastTime{};

	int32_t sizeLeftUntilBufferSwap{};

	int32_t prevFeedback = 0;

	uint8_t repeatsUntilAbandon = 0; // 0 means never abandon

	/// @brief Renders one block of the delay into @p buffer: the lifecycle around the processor's block (the rate's
	/// settling and a secondary tape grown before it; the swap and the abandon count after it).
	/// @param renderInStereo whether the engine renders in stereo: ping-pong crosses channels only then.
	void process(std::span<StereoSample> buffer, const State& delayWorkingState, bool renderInStereo);

private:
	void prepareToBeginWriting();
	[[nodiscard]] constexpr int32_t getAmountToWriteBeforeReadingBegins() const {
		return secondaryBuffer.tape().size();
	}
};
