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

#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/delay/delay_tape.h"
#include "util/fixedpoint.h"

#include <cstddef>
#include <cstdint>

/// @file
/// @brief Stutter: the stutter as one frame kernel that records a tape, then plays it back.
namespace deluge::dsp::fixed {

/// @brief The stutter over a tape it is handed, as a processor kernel of stereo frames, rendered in place as
/// `Mono<Stutter>::renderBlock(block, block)`. Both channels share one tape head.
/// @details
/// **Record.** Each frame the read head clears the sample under it and moves on, counting down the frames left (a
/// resampled tape advances by the spin, clearing and counting each sample it passes), and the frame is written
/// kWriteLag samples behind it: natively as it is, resampled as the triangle the advance's 16.16 strengths give. The
/// frame passes through unchanged.
///
/// **Play.** Each frame the head steps forward, or back when reversed, and the frame becomes the tape's sample under
/// it; a resampled tape advances or retreats by the spin and weighs the sample under the head with the next one by the
/// strengths, whichever way it travels. With ping-pong, the direction turns when the head reaches either end of the
/// tape.
///
/// **Per block.** The owner configures each block with the rate, the tape and whether the stutter plays; @ref prepare
/// sets the tape up for the rate once. When a recording block's frames ran out (`!playing && frames_left < 0`), the
/// owner sends @ref finish_block after the block, which puts the head at the tape's start (its end when reversed) and
/// turns to play: never mid-block.
///
/// **External memory.** The step writes the tape its Coefficients point at, which the owner owns and hands it each
/// block. Configure before every block: the coefficients read the tape's form, which the block's setup may change.
struct Stutter {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.

	/// @brief Whether the tape is being recorded or played.
	enum class Phase : uint8_t {
		RECORD, ///< The input written to the tape and passed through.
		PLAY,   ///< The tape played back in place of the input.
	};
	/// @brief How the tape moves this block.
	enum class Tape : uint8_t {
		NATIVE,    ///< One step per frame.
		RESAMPLED, ///< Stepped by the spin, read and written by 16.16 strengths.
	};

	/// @brief The block's mode: one loop per combination.
	struct Mode {
		Phase phase = Phase::RECORD; ///< Recording or playing.
		Tape tape = Tape::NATIVE;    ///< How the tape moves.

		/// @brief Whether every member matches.
		[[nodiscard]] [[gnu::always_inline]] constexpr bool operator==(const Mode&) const = default;
	};

	/// @brief Every mode, the default first.
	using Modes = ModeList<Mode{Phase::RECORD, Tape::NATIVE}, Mode{Phase::RECORD, Tape::RESAMPLED},
	                       Mode{Phase::PLAY, Tape::NATIVE}, Mode{Phase::PLAY, Tape::RESAMPLED}>;

	/// @brief One block's settings: the rate, the tape and the phase.
	struct Settings {
		/// The tape speed, 8.24 on the delay's rate scale, as the stutter-rate param and the tempo give it. Unsigned,
		/// as the tape sizes for it; the tape's setup takes its int32_t image.
		uint32_t rate = 0;
		DelayTape* tape = nullptr; ///< The tape, active.
		bool playing = false;      ///< The tape has been recorded: State::playing after the last block.
	};

	/// @brief What a step reads: the tape, the rate and the block's mode.
	struct Coefficients {
		DelayTape* tape = nullptr; ///< The tape, as Settings::tape.
		uint32_t rate = 0;         ///< The tape speed, 8.24, as Settings::rate.
		Mode mode{};               ///< The block's mode, which the wrapper dispatches on.
	};

	/// @brief The recording's countdown and the playback's direction.
	struct State {
		int32_t frames_left = 0; ///< Samples the read head has left to pass while recording; below zero, recorded.
		bool reverse = false;    ///< Playback runs backwards.
		bool ping_pong = false;  ///< Playback turns at either end of the tape.
		bool playing = false;    ///< The tape has been recorded and plays.
	};

	/// @brief Nothing shared across a block's steps beyond the coefficients.
	struct Block {};

	/// @brief The State lives in its wrapper through the block, as the frames left count down in place.
	static constexpr bool kStateInPlace = true;

	/// @brief The coefficients for @p s. The tape form is the tape's after this block's setup, by the rule the setup
	/// itself takes (DelayTape::resamplesAt).
	[[nodiscard]] [[gnu::always_inline]] static Coefficients coefficients(const Settings& s) {
		return {.tape = s.tape,
		        .rate = s.rate,
		        .mode = {.phase = s.playing ? Phase::PLAY : Phase::RECORD,
		                 .tape = s.tape->resamplesAt(s.rate) ? Tape::RESAMPLED : Tape::NATIVE}};
	}

	/// @brief Sets the tape up for the block's rate.
	[[nodiscard]] [[gnu::always_inline]] static Block prepare(const Coefficients& c, State& /*s*/, std::size_t /*n*/) {
		// The tape takes the rate's int32_t image.
		c.tape->setupForRender(static_cast<int32_t>(c.rate));
		return {};
	}

	/// @brief One frame at mode @p M; see the class documentation.
	/// @param in The frame coming in, Q31.
	/// @return While recording, @p in; while playing, the tape's frame, Q31.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static StereoSample step(const Coefficients& c, const Block& /*b*/, State& s,
	                                                              StereoSample in) {
		DelayTape& tape = *c.tape;
		if constexpr (M.phase == Phase::RECORD) {
			if constexpr (M.tape == Tape::NATIVE) {
				tape.clearAndMoveOn();
				s.frames_left--;
				tape.writeNative(in);
			}
			else {
				const int32_t strength2 = tape.advance([&] [[gnu::always_inline]] {
					tape.clearAndMoveOn();
					s.frames_left--;
				});
				tape.writeResampled(in, 65536 - strength2, strength2);
			}
			return in;
		}
		else {
			StereoSample out;
			if constexpr (M.tape == Tape::NATIVE) {
				if (s.reverse) {
					tape.moveBack();
				}
				else {
					tape.moveOn();
				}
				out = tape.current();
			}
			else {
				// Both directions give the strength toward the next sample, so the head interpolates the same positions
				// either way.
				const int32_t strength2 = s.reverse ? tape.retreat([&] [[gnu::always_inline]] { tape.moveBack(); })
				                                    : tape.advance([&] [[gnu::always_inline]] { tape.moveOn(); });
				StereoSample* other = &tape.current() + 1;
				if (other == tape.end()) {
					other = tape.begin();
				}
				const int32_t strength1 = 65536 - strength2;
				out = {.l = mix(tape.current().l, other->l, strength1, strength2),
				       .r = mix(tape.current().r, other->r, strength1, strength2)};
			}
			if (s.ping_pong
			    && ((s.reverse && &tape.current() == tape.begin())
			        || (!s.reverse && &tape.current() == tape.end() - 1))) {
				s.reverse = !s.reverse;
			}
			return out;
		}
	}

	/// @brief Event: recording begins over a tape of @p frames played samples, then plays @p reversed, turning at
	/// each end with @p ping_pong.
	static void start(State& s, int32_t frames, bool reversed, bool ping_pong) {
		s = {.frames_left = frames, .reverse = reversed, .ping_pong = ping_pong, .playing = false};
	}

	/// @brief Event, after a recording block whose frames ran out: the head to @p tape's start (its end when
	/// reversed), then play.
	static void finish_block(State& s, DelayTape& tape) {
		tape.setCurrent(s.reverse ? tape.end() - 1 : tape.begin());
		s.playing = true;
	}

private:
	/// @return The taps @p a and @p b weighed by @p strength1 and @p strength2 (16.16, summing to 65536), Q31. The sum
	/// wraps, as the hardware's add does, and the shift goes through uint32_t.
	[[nodiscard]] [[gnu::always_inline]] static q31 mix(q31 a, q31 b, int32_t strength1, int32_t strength2) {
		const q31 sum =
		    wrapping_add(multiply_32x32_rshift32(a, strength1 << 14), multiply_32x32_rshift32(b, strength2 << 14));
		return static_cast<q31>(static_cast<uint32_t>(sum) << 2);
	}
};

} // namespace deluge::dsp::fixed
