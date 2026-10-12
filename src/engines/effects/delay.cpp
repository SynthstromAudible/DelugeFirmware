/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "engines/effects/delay.h"

#include "util/fixedpoint.h"

#include <algorithm>
#include <atomic>

namespace deluge::effects {

using dsp::fixed::DelayTape;

namespace {

/// Frames the rate must hold still before a tape is made at it: an eighth of a second at 44.1 kHz.
constexpr uint32_t kSettleFrames = 44100 >> 3;

/// Blocks in which an OFF delay had feedback enough to sound and sound coming in.
std::atomic<uint32_t> g_offSilencedBlocks{0};

} // namespace

uint32_t delay_off_silenced_blocks() {
	return g_offSilencedBlocks.load(std::memory_order_relaxed);
}

TapeShape tape_shape(uint32_t rate, bool include_extra_space) {
	const auto [size, clamped] = DelayTape::idealSizeForRate(rate);
	const auto played = static_cast<std::size_t>(size);
	return {.size = played,
	        .frames = played + (include_extra_space ? static_cast<std::size_t>(DelayTape::kWriteLag) : 0),
	        .clamped = clamped};
}

DelayTape make_tape(std::span<dsp::StereoSample> storage, TapeShape shape, uint32_t rate) {
	DelayTape tape{storage.first(shape.frames), shape.size, rate};
	if (shape.clamped) {
		tape.makeNativeRatePrecise();
	}
	return tape;
}

Delay::Delay(DelayArena& arena) : arena_{&arena} {
}

bool Delay::takeTape(DelayTape& tape, uint32_t rate, uint32_t failIfThisSize) {
	const TapeShape shape = tape_shape(rate, true);
	if (static_cast<uint32_t>(shape.size) == failIfThisSize) {
		return false;
	}
	dsp::StereoSample* storage = arena_->take(shape.frames);
	if (storage == nullptr) {
		tape = {};
		return false;
	}
	tape = make_tape({storage, shape.frames}, shape, rate);
	return true;
}

void Delay::giveBack(DelayTape& tape) {
	if (tape.isActive()) {
		arena_->give_back(tape.begin(), tape.sizeIncludingExtra());
		tape = {};
	}
}

void Delay::informWhetherActive(bool newActive, int32_t userDelayRate) {
	const bool previouslyActive = isActive();

	if (previouslyActive != newActive) {
		if (!newActive) {
			discardBuffers();
			return;
		}
		if (!takeTape(secondary_, userDelayRate, 0)) {
			return;
		}
		prepareToBeginWriting();
		processor_.resetHighPass();
		return;
	}

	// Already active, with nothing written yet: a secondary sized for a rate that has since changed is replaced
	// before writing starts.
	if (previouslyActive && !primary_.isActive() && secondary_.isActive()
	    && sizeLeftUntilBufferSwap_ == getAmountToWriteBeforeReadingBegins()) {
		const auto [idealBufferSize, clamped] = DelayTape::idealSizeForRate(userDelayRate);
		if (static_cast<std::size_t>(idealBufferSize) != secondary_.size()) {
			giveBack(secondary_);
			if (!takeTape(secondary_, userDelayRate, 0)) {
				return;
			}
			prepareToBeginWriting();
			processor_.resetHighPass();
		}
	}
}

void Delay::copySecondaryToPrimary() {
	giveBack(primary_);
	primary_ = secondary_;
	secondary_ = {};
}

void Delay::prepareToBeginWriting() {
	sizeLeftUntilBufferSwap_ = getAmountToWriteBeforeReadingBegins();
}

void Delay::setupWorkingState(State& workingState, const DelayState& settings, uint32_t timePerInternalTickInverse,
                              bool anySoundComingIn) {
	workingState.ping_pong = settings.ping_pong;
	workingState.analog = settings.type == DelayType::ANALOG;
	const engine::SyncLevel syncLevel = settings.sync_level;
	const engine::SyncType syncType = settings.sync_type;

	// The patching engine gives a feedback of 112 for a knob at its minimum, so 256 is the least that sounds.
	const bool off = settings.type == DelayType::OFF;
	if (off && workingState.delayFeedbackAmount >= 256 && anySoundComingIn) {
		g_offSilencedBlocks.fetch_add(1, std::memory_order_relaxed);
	}
	const bool mightDoDelay =
	    !off && workingState.delayFeedbackAmount >= 256 && (anySoundComingIn || repeatsUntilAbandon_ != 0);

	if (mightDoDelay && syncLevel != engine::SyncLevel::NONE) {
		workingState.userDelayRate = multiply_32x32_rshift32_rounded(workingState.userDelayRate,
		                                                             static_cast<int32_t>(timePerInternalTickInverse));
		// Limit to the biggest number we can store.
		const int32_t limit = 2147483647 >> (engine::index_of(syncLevel) + 5);
		workingState.userDelayRate = std::min(workingState.userDelayRate, limit);
		if (syncType == engine::SyncType::TRIPLET) {
			workingState.userDelayRate = workingState.userDelayRate * 3 / 2;
		}
		else if (syncType == engine::SyncType::DOTTED) {
			workingState.userDelayRate = workingState.userDelayRate * 2 / 3;
		}
		workingState.userDelayRate <<= (engine::index_of(syncLevel) + 5);
	}

	informWhetherActive(mightDoDelay, workingState.userDelayRate);
	workingState.doDelay = isActive(); // whether a tape was taken

	if (workingState.doDelay) {
		// When feedback has changed, or sound is coming in, reassess how long to leave the delay sounding for.
		if (anySoundComingIn || workingState.delayFeedbackAmount != prevFeedback_) {
			setTimeToAbandon(workingState);
			prevFeedback_ = workingState.delayFeedbackAmount;
		}
	}
}

void Delay::setTimeToAbandon(const State& workingState) {
	const int32_t f = workingState.delayFeedbackAmount;
	if (!workingState.doDelay) {
		repeatsUntilAbandon_ = 0;
	}
	else if (f < 33554432) {
		repeatsUntilAbandon_ = 1;
	}
	else if (f <= 100663296) {
		repeatsUntilAbandon_ = 2;
	}
	else if (f <= 218103808) {
		repeatsUntilAbandon_ = 3;
	}
	else if (f < 318767104) {
		repeatsUntilAbandon_ = 4;
	}
	else if (f < 352321536) {
		repeatsUntilAbandon_ = 5;
	}
	else if (f < 452984832) {
		repeatsUntilAbandon_ = 6;
	}
	else if (f < 520093696) {
		repeatsUntilAbandon_ = 9;
	}
	else if (f < 637534208) {
		repeatsUntilAbandon_ = 12;
	}
	else if (f < 704643072) {
		repeatsUntilAbandon_ = 13;
	}
	else if (f < 771751936) {
		repeatsUntilAbandon_ = 18;
	}
	else if (f < 838860800) {
		repeatsUntilAbandon_ = 24;
	}
	else if (f < 939524096) {
		repeatsUntilAbandon_ = 40;
	}
	else if (f < 1040187392) {
		repeatsUntilAbandon_ = 110;
	}
	else {
		repeatsUntilAbandon_ = 255;
	}
}

void Delay::hasWrapped() {
	if (repeatsUntilAbandon_ == 255) {
		return;
	}
	repeatsUntilAbandon_--;
	if (repeatsUntilAbandon_ == 0) {
		discardBuffers();
	}
}

void Delay::discardBuffers() {
	giveBack(primary_);
	giveBack(secondary_);
	prevFeedback_ = 0;
	repeatsUntilAbandon_ = 0;
}

void Delay::initializeSecondaryBuffer(int32_t newNativeRate, bool makeNativeRatePreciseRelativeToOtherBuffer) {
	if (!takeTape(secondary_, static_cast<uint32_t>(newNativeRate), static_cast<uint32_t>(primary_.size()))) {
		return;
	}
	if (makeNativeRatePreciseRelativeToOtherBuffer) {
		primary_.makeNativeRatePreciseRelativeTo(secondary_);
	}
	else {
		primary_.makeNativeRatePrecise();
		secondary_.makeNativeRatePrecise();
	}
	sizeLeftUntilBufferSwap_ = static_cast<int32_t>(secondary_.size()) + 5;
}

void Delay::process(std::span<dsp::StereoSample> buffer, const State& delayWorkingState, bool renderInStereo) {
	if (!delayWorkingState.doDelay) {
		return;
	}

	if (delayWorkingState.userDelayRate != userRateLastTime_) {
		userRateLastTime_ = delayWorkingState.userDelayRate;
		countCyclesWithoutChange_ = 0;
	}
	else {
		countCyclesWithoutChange_ += buffer.size();
	}

	// With one tape reading and writing, a second may be started at a new size.
	if (!secondary_.isActive()) {
		const auto rate = static_cast<uint32_t>(delayWorkingState.userDelayRate);
		if (primary_.resampling() || rate != primary_.nativeRate()) {
			// The rate has settled for a split second: a tape native to it.
			if (countCyclesWithoutChange_ >= kSettleFrames) {
				initializeSecondaryBuffer(delayWorkingState.userDelayRate, true);
			}
			// At double the native rate or more the tape is needlessly large: a smaller one at the new rate.
			else if (rate >= (primary_.nativeRate() << 1)) {
				initializeSecondaryBuffer(delayWorkingState.userDelayRate, false);
			}
			// Below half the native rate the quality suffers: a tape whose native rate is half the new rate.
			else if (rate < primary_.nativeRate() >> 1) {
				initializeSecondaryBuffer(delayWorkingState.userDelayRate >> 1, false);
			}
		}
	}

	DelayTape* const growing = secondary_.isActive() ? &secondary_ : nullptr;
	const dsp::fixed::DelayBlockResult result =
	    processor_.process(buffer, &primary_, growing,
	                       {.rate = delayWorkingState.userDelayRate,
	                        .feedback = delayWorkingState.delayFeedbackAmount,
	                        .analog = delayWorkingState.analog,
	                        .analog_saturation = delayWorkingState.analog_saturation,
	                        .ping_pong = delayWorkingState.ping_pong,
	                        .render_in_stereo = renderInStereo});

	if (growing != nullptr) {
		sizeLeftUntilBufferSwap_ -= result.secondary_written;
		if (sizeLeftUntilBufferSwap_ < 0) {
			copySecondaryToPrimary();
		}
	}

	if (result.wrapped) {
		hasWrapped();
	}
}

DelayChange Delay::prepare(DelayType type) {
	const int on_delta = static_cast<int>(type != DelayType::OFF) - static_cast<int>(counted_);
	return {.arena = arena_->prepare(on_delta)};
}

void Delay::commit(DelayType type, DelayChange& change) {
	if (type == DelayType::OFF) {
		discardBuffers();
	}
	arena_->commit(change.arena);
	counted_ = type != DelayType::OFF;
}

void Delay::finish(DelayChange& change) {
	arena_->finish(change.arena);
}

} // namespace deluge::effects
