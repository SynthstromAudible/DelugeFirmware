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

#pragma once

#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/delay/delay_processor.h"
#include "dsp-next/fixed/delay/delay_tape.h"
#include "engines/effects/delay_arena.h"
#include "engines/effects/state.h"
#include "engines/effects/words.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace deluge::effects {

/// @brief A tape's shape for a delay at a rate: its played size, its storage in frames, and whether the size was
/// clamped to the tape's range.
struct TapeShape {
	std::size_t size;   ///< the played size, in frames
	std::size_t frames; ///< the storage, in frames: the size, plus the write lag when asked for
	bool clamped;       ///< the ideal size lay outside [kMinSize, kMaxSize]
};

/// @return The tape's shape for a delay at @p rate (16777216 is the neutral rate): its ideal size, and the write lag's
/// frames more when @p include_extra_space, for the resampled write's run-off.
[[nodiscard]] TapeShape tape_shape(uint32_t rate, bool include_extra_space = true);

/// @return The tape over @p storage (shape.frames frames) for a delay at @p rate, its native rate made precise when
/// its size was clamped.
[[nodiscard]] dsp::fixed::DelayTape make_tape(std::span<dsp::StereoSample> storage, TapeShape shape, uint32_t rate);

/// @brief A type change's tape memory: the arena's change, made before the owner's audio-excluded section.
struct DelayChange {
	ArenaChange arena;
};

/// @return The rate a delay at @p userDelayRate (16777216 the neutral rate) plays at: as given unless @p settings sync
/// it, then scaled by @p timePerInternalTickInverse to the sync level and type.
[[nodiscard]] int32_t synced_delay_rate(int32_t userDelayRate, const DelayState& settings,
                                        uint32_t timePerInternalTickInverse);

/// @return How many blocks a delay that is OFF has been asked to sound: feedback enough to start, with sound coming in.
[[nodiscard]] uint32_t delay_off_silenced_blocks();

/// @brief An owner's delay: the dsp-next delay processor over a primary tape and, while the rate settles at a new
/// size, a secondary tape that grows until it is swapped in; the tapes come from the arena and go back to it.
///
/// The render starts the delay when its feedback can sound and sound comes in, takes a new tape when the rate moves
/// far enough from the primary's, swaps it in once written, and gives the tapes back after the repeats its feedback
/// allows, all at the blocks they happen on. The interaction tier counts the owner in the arena's margin while its
/// type is not OFF, and gives its tapes back when it is set OFF, in prepare / commit / finish around the owner's
/// audio-excluded section.
class Delay {
public:
	/// The block's working state: the rate and feedback its owner derived, and the owner's settings it plays with.
	struct State {
		bool doDelay{};
		int32_t userDelayRate{};       ///< the rate, 16777216 the neutral rate
		int32_t delayFeedbackAmount{}; ///< the feedback, Q31; below 256 the delay does not start
		int32_t analog_saturation = 8;
		bool ping_pong{true};
		bool analog{false};
	};

	explicit Delay(DelayArena& arena);
	Delay(const Delay&) = delete;
	Delay& operator=(const Delay&) = delete;

	[[nodiscard]] constexpr bool isActive() const { return primary_.isActive() || secondary_.isActive(); }
	/// @return The wraps left before the tapes are given back; 0 when the delay is not sounding, 255 never.
	[[nodiscard]] constexpr uint8_t repeatsUntilAbandon() const { return repeatsUntilAbandon_; }

	/// @brief [render] Fills @p workingState's rate (synced to @p settings' level and type when it is synced) and
	/// settings for a block, and starts or stops the delay.
	void setupWorkingState(State& workingState, const DelayState& settings, uint32_t timePerInternalTickInverse,
	                       bool anySoundComingIn = true);
	/// @brief [render] Renders one block of the delay into @p buffer: the rate's settling and a secondary tape before
	/// the processor's block; the swap and the abandon count after it.
	/// @param renderInStereo whether the engine renders in stereo: ping-pong crosses channels only then.
	void process(std::span<dsp::StereoSample> buffer, const State& delayWorkingState, bool renderInStereo);
	/// @brief Gives both tapes back to the arena. The render's, or an owner's inside its audio-excluded section.
	void discardBuffers();

	/// @brief [task] Before the section: the arena's change for @p type, counting the owner in or out.
	[[nodiscard]] DelayChange prepare(DelayType type);
	/// @brief [task] Inside the section: gives the tapes back and closes the owner's slot when @p type is OFF;
	/// otherwise opens or updates its slot, reserving the largest of a neutral-rate tape, @p reserveFrames (the tape
	/// its settings need) and the largest tape it has taken; then commits the arena's change.
	void commit(DelayType type, DelayChange& change, std::size_t reserveFrames = 0);
	/// @brief [task, inside the section] Updates a counted owner's reserve to @p reserveFrames, as commit does.
	void setReserve(std::size_t reserveFrames);
	/// @brief [task] After the section.
	void finish(DelayChange& change);
	/// @return Whether the owner is counted on in the arena's margin.
	[[nodiscard]] bool counted() const { return counted_; }
	/// @return The reserve for @p reserveFrames: at least a neutral-rate tape and the largest tape taken.
	[[nodiscard]] std::size_t reserveFor(std::size_t reserveFrames) const;
	/// @return The owner's reserve in the arena, in frames, while counted; else 0.
	[[nodiscard]] std::size_t reserve() const { return counted_ ? arena_->reserve(slot_) : 0; }

private:
	void informWhetherActive(bool newActive, int32_t userDelayRate = 0);
	void copySecondaryToPrimary();
	void initializeSecondaryBuffer(int32_t newNativeRate, bool makeNativeRatePreciseRelativeToOtherBuffer);
	void setTimeToAbandon(const State& workingState);
	void hasWrapped();
	void prepareToBeginWriting();
	/// @return Whether a tape was taken for @p rate into @p tape: not when its ideal size is @p failIfThisSize (the
	/// tape left as it was), nor when the arena has no room (the tape left without storage).
	bool takeTape(dsp::fixed::DelayTape& tape, uint32_t rate, uint32_t failIfThisSize);
	/// @brief Gives @p tape's storage back to the arena, if it has any.
	void giveBack(dsp::fixed::DelayTape& tape);
	[[nodiscard]] constexpr int32_t getAmountToWriteBeforeReadingBegins() const {
		return static_cast<int32_t>(secondary_.size());
	}

	DelayArena* arena_;
	std::size_t slot_ = 0;         ///< the owner's reserve in the arena, while counted
	std::size_t largestTaken_ = 0; ///< the largest tape taken since the owner was counted, in frames
	dsp::fixed::DelayTape primary_;
	dsp::fixed::DelayTape secondary_;
	/// The block's DSP: the read, the feedback's shaping and high-pass, the mix and the writes.
	dsp::fixed::DelayProcessor processor_;
	uint32_t countCyclesWithoutChange_{};
	int32_t userRateLastTime_{};
	int32_t sizeLeftUntilBufferSwap_{};
	int32_t prevFeedback_ = 0;
	uint8_t repeatsUntilAbandon_ = 0; ///< wraps left before the tapes are given back; 255 never
	bool counted_ = false;
};

} // namespace deluge::effects
