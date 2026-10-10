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

#include "dsp-next/core/chain.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/delay/delay_tape.h"
#include "dsp-next/fixed/delay/feedback_shaper.h"
#include "dsp-next/fixed/filter/shift_tracker.h"
#include "util/fixedpoint.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief Delay: the delay as one fused frame kernel that reads the tape, runs the feedback, mixes and writes the tape.
namespace deluge::dsp::fixed {

/// @brief The delay's feedback stages, Q31: shaped by the amount and saturated (@ref FeedbackShaper), then
/// high-passed by a one-pole low-pass at a 1/2048 step taken out of it, so repeats don't build up sub-bass.
using FeedbackChain = Chain<FeedbackShaper, Pinned<ShiftTracker<11>, ShiftTracker<11>::Mode::REMOVE>>;

/// @brief The delay over tapes it is handed, as a processor kernel of stereo frames: each frame reads the primary tape,
/// runs the feedback, mixes and writes both tapes before the next frame is read. Rendered in place as
/// `Mono<Delay>::renderBlock(block, block)`.
/// @details
/// **Per frame** (`step`), in order:
/// 1. **Read.** With `reading`, the primary is read at its read head, which clears each sample it leaves and notes a
///    wrap: natively the sample under the head after one step; resampled, the head advanced by the spin (8.24) and the
///    samples under it and after it weighed by the 16.16 strengths the advance gives. Without `reading`, silence.
/// 2. **Feedback.** @ref FeedbackChain steps the left channel through `State::left`, then the right through
///    `State::right`, at the chain mode that matches @ref Mode::shaper.
/// 3. **Mix.** Straight, the tape takes the dry frame plus the feedback. Crossed (ping-pong, rendering in stereo), the
///    left takes the right's feedback and the right takes the dry channels' mean plus the left's feedback.
/// 4. **Write.** With `reading`, the primary is written kWriteLag samples behind the head where this frame's read left
///    it: natively the sample behind where the read began, resampled the triangle the read's own strengths give.
///    Following the read is the same as walking the head again from where it stood, without the second walk. With
///    `growing`, the secondary is written too, clearing ahead of its own head and counting its steps.
/// 5. **Return** the dry frame plus the feedback.
///
/// Each frame's write lands before the next frame's read, so an echo at a delay shorter than the block comes round
/// within the block as the delay line plays it (.superpowers/findings/delay-lap/).
///
/// **External memory.** The step writes the tapes its Coefficients point at: memory outside its State, which the owner
/// owns and hands it per block. The tapes' heads move as they are read and written. Configure before every block: the
/// coefficients read each tape's form, which the owner's lifecycle changes between blocks.
///
/// **Results.** `State::wrapped` and `State::secondary_written` accumulate over the block; the owner reads and clears
/// them after it. `wrapped` is the secondary's wrap while one grows, else the primary's, as the owner's abandon count
/// takes it.
///
/// **Reactivation.** @ref reset_high_pass resets each channel's high-pass memory and keeps its impulse response
/// history, so a delay switched off and on again resumes the response's tail.
///
/// **Hot path.** The tape, the response and the chain are stepped through their inline members, and the block's
/// storage bounds are taken once per block (@ref Block). Under the DSP library's flags, where the kernel is
/// instantiated, the eight loops make no call; the tape's out-of-line `setupForRender` runs once per block in
/// @ref prepare.
struct Delay {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.

	/// @brief The feedback chain the delay steps on each channel.
	using FeedbackChain = fixed::FeedbackChain;

	/// @brief How the feedback is saturated: @ref FeedbackShaper's modes.
	enum class Shaper : uint8_t {
		DIGITAL, ///< Clipped to 29 bits.
		ANALOG,  ///< Through the impulse response, then a tanh at the drive.
	};
	/// @brief How the feedback is mixed back to the tape.
	enum class Mix : uint8_t {
		STRAIGHT, ///< Each channel's feedback to its own channel.
		CROSS,    ///< Ping-pong: the channels' feedback crossed.
	};
	/// @brief How the primary tape plays this block.
	enum class Tape : uint8_t {
		NATIVE,    ///< One step per frame.
		RESAMPLED, ///< Stepped by the spin, read and written by 16.16 strengths.
	};

	/// @brief The block's mode: one loop per combination, with no branch in it on these choices.
	struct Mode {
		Shaper shaper = Shaper::DIGITAL; ///< How the feedback is saturated.
		Mix mix = Mix::STRAIGHT;         ///< How the feedback is mixed back.
		Tape tape = Tape::NATIVE;        ///< How the primary plays.

		/// @brief Whether every member matches.
		[[nodiscard]] [[gnu::always_inline]] constexpr bool operator==(const Mode&) const = default;
	};

	/// @brief Every mode, the default first: shaper slowest, tape fastest.
	// clang-format off
	using Modes = ModeList<Mode{Shaper::DIGITAL, Mix::STRAIGHT, Tape::NATIVE},
	                       Mode{Shaper::DIGITAL, Mix::STRAIGHT, Tape::RESAMPLED},
	                       Mode{Shaper::DIGITAL, Mix::CROSS, Tape::NATIVE},
	                       Mode{Shaper::DIGITAL, Mix::CROSS, Tape::RESAMPLED},
	                       Mode{Shaper::ANALOG, Mix::STRAIGHT, Tape::NATIVE},
	                       Mode{Shaper::ANALOG, Mix::STRAIGHT, Tape::RESAMPLED},
	                       Mode{Shaper::ANALOG, Mix::CROSS, Tape::NATIVE},
	                       Mode{Shaper::ANALOG, Mix::CROSS, Tape::RESAMPLED}>;
	// clang-format on

	/// @brief One block's settings as the owner resolved them, and the tapes it plays.
	struct Settings {
		uint32_t rate = 0;              ///< The tape speed, unsigned as the tape sizes for it: 16777216 is neutral.
		q31 feedback = 0;               ///< The feedback amount, Q31.
		bool analog = false;            ///< The impulse response then a tanh; else a digital clip.
		int32_t analog_saturation = 8;  ///< The tanh drive, as a power of two: 8 on a sound, 5 on a host chain.
		bool ping_pong = false;         ///< The feedback crosses between the channels when rendering in stereo.
		bool render_in_stereo = true;   ///< Whether the engine renders in stereo.
		DelayTape* primary = nullptr;   ///< The tape read and written back; null or inactive reads silence.
		DelayTape* secondary = nullptr; ///< A tape grown alongside, or null.
	};

	/// @brief What a step reads: the chain's coefficients, the tapes, and the block's form.
	struct Coefficients {
		FeedbackChain::Coefficients feedback{}; ///< The chain's, built by FeedbackChain::coefficients.
		DelayTape* primary = nullptr;           ///< The tape read and written back, or null.
		DelayTape* secondary = nullptr;         ///< The tape grown alongside, or null.
		uint32_t rate = 0;                      ///< The tape speed, as Settings::rate.
		bool reading = false;                   ///< The primary is set and active: it is read and written.
		bool growing = false;                   ///< The secondary is set and active: it is written.
		Mode mode{};                            ///< The block's mode, which the wrapper dispatches on.
	};

	/// @brief Each channel's feedback memories, and what the block did to the tapes.
	struct State {
		FeedbackChain::State left{};  ///< The left channel's impulse response history and high-pass memory.
		FeedbackChain::State right{}; ///< The right channel's.
		bool wrapped = false; ///< A read head wrapped this block: the secondary's while it grows, else the primary's.
		int32_t secondary_written = 0; ///< The secondary's read-head steps this block, in tape samples.
	};

	/// @brief What a block's steps share, set up once per block by @ref prepare.
	struct Block {
		StereoSample* primary_begin = nullptr; ///< The primary's first sample: a native write wraps back past it.
		std::ptrdiff_t primary_ring = 0;       ///< The primary's storage, in samples.
		bool secondary_resampled = false;      ///< Whether the growing secondary resamples this block.
	};

	/// @brief The kernel's State lives in its wrapper through the block: it holds two 25-word histories, too much for
	/// registers.
	static constexpr bool kStateInPlace = true;

	/// @brief The chain mode that matches @p shaper.
	[[nodiscard]] [[gnu::always_inline]] static constexpr FeedbackChain::Mode feedback_mode(Shaper shaper) {
		return {shaper == Shaper::ANALOG ? FeedbackShaper::Mode::ANALOG : FeedbackShaper::Mode::DIGITAL};
	}

	/// @brief The coefficients for @p s.
	/// @details The chain's coefficients and mode come from FeedbackChain::coefficients, and the shaper from the
	/// chain's mode, so the two agree. The mix crosses when `ping_pong && render_in_stereo`. The tape form is the
	/// primary's after this block's setup: a tape resampling stays so, and a native one starts resampling at any rate
	/// but its native rate. The secondary's form is decided per block in @ref prepare, after its own setup.
	[[nodiscard]] [[gnu::always_inline]] static Coefficients coefficients(const Settings& s) {
		Coefficients c{
		    .feedback = FeedbackChain::coefficients(FeedbackChain::Settings{
		        FeedbackShaper::Settings{
		            .feedback = s.feedback, .analog = s.analog, .drive = static_cast<uint32_t>(s.analog_saturation)},
		        ShiftTracker<11>::Settings{},
		    }),
		    .primary = s.primary,
		    .secondary = s.secondary,
		    .rate = s.rate,
		    .reading = s.primary != nullptr && s.primary->isActive(),
		    .growing = s.secondary != nullptr && s.secondary->isActive(),
		    .mode = {},
		};
		c.mode.shaper = get<0>(c.feedback.mode) == FeedbackShaper::Mode::ANALOG ? Shaper::ANALOG : Shaper::DIGITAL;
		c.mode.mix = s.ping_pong && s.render_in_stereo ? Mix::CROSS : Mix::STRAIGHT;
		const bool resampled = c.reading && (s.primary->resampling() || s.rate != s.primary->nativeRate());
		c.mode.tape = resampled ? Tape::RESAMPLED : Tape::NATIVE;
		return c;
	}

	/// @brief Sets each tape up for the block's rate, as the owner's block does: the primary whenever it is set, the
	/// secondary while it grows.
	/// @return Where the primary's storage begins and how long it is, and whether the secondary resamples.
	[[nodiscard]] [[gnu::always_inline]] static Block prepare(const Coefficients& c, State& /*s*/, std::size_t /*n*/) {
		// The tape takes the rate's int32_t image.
		const auto rate = static_cast<int32_t>(c.rate);
		if (c.primary != nullptr) {
			c.primary->setupForRender(rate);
		}
		Block block{};
		if (c.reading) {
			block.primary_begin = c.primary->begin();
			block.primary_ring = static_cast<std::ptrdiff_t>(c.primary->sizeIncludingExtra());
		}
		if (c.growing) {
			c.secondary->setupForRender(rate);
			block.secondary_resampled = c.secondary->resampling();
		}
		return block;
	}

	/// @brief One frame at mode @p M: read, feed back, mix, write; see the class documentation.
	/// @param dry The frame coming in, Q31.
	/// @return The dry frame plus the feedback, Q31.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static StereoSample step(const Coefficients& c, const Block& b, State& s,
	                                                              StereoSample dry) {
		StereoSample delayed{.l = 0, .r = 0};
		bool wrapped = false;
		// The native write's sample, and the resampled write's strengths (16.16): where this frame's read leaves them.
		StereoSample* native_write = nullptr;
		int32_t strength1 = 0;
		int32_t strength2 = 0;
		if (c.reading) {
			DelayTape& primary = *c.primary;
			if constexpr (M.tape == Tape::NATIVE) {
				std::ptrdiff_t index = (&primary.current() - b.primary_begin) - DelayTape::kWriteLag;
				if (index < 0) {
					index += b.primary_ring;
				}
				native_write = b.primary_begin + index;
				wrapped = primary.clearAndMoveOn();
				delayed = primary.current();
			}
			else {
				strength2 =
				    primary.advance([&] [[gnu::always_inline]] { wrapped = primary.clearAndMoveOn() || wrapped; });
				strength1 = 65536 - strength2;
				StereoSample* next = &primary.current() + 1;
				if (next == primary.end()) {
					next = primary.begin();
				}
				const StereoSample from1 = primary.current();
				const StereoSample from2 = *next;
				delayed.l = (multiply_32x32_rshift32(from1.l, strength1 << 14)
				             + multiply_32x32_rshift32(from2.l, strength2 << 14))
				            << 2;
				delayed.r = (multiply_32x32_rshift32(from1.r, strength1 << 14)
				             + multiply_32x32_rshift32(from2.r, strength2 << 14))
				            << 2;
			}
		}

		constexpr FeedbackChain::Mode kFeedback = feedback_mode(M.shaper);
		constexpr kernel_block_t<FeedbackChain> kNoBlock{};
		const StereoSample feedback{
		    .l = kernel_ops::step_at<FeedbackChain, kFeedback>(c.feedback, kNoBlock, s.left, delayed.l),
		    .r = kernel_ops::step_at<FeedbackChain, kFeedback>(c.feedback, kNoBlock, s.right, delayed.r),
		};

		StereoSample to_tape = feedback;
		if constexpr (M.mix == Mix::CROSS) {
			to_tape.l = feedback.r;
			to_tape.r = ((dry.l + dry.r) >> 1) + feedback.l;
		}
		else {
			to_tape += dry;
		}

		if (c.reading) {
			if constexpr (M.tape == Tape::NATIVE) {
				*native_write = to_tape;
			}
			else {
				c.primary->writeResampled(to_tape, strength1, strength2);
			}
		}
		if (c.growing) {
			// The secondary's wraps replace the primary's: it is the tape that will play on.
			wrapped = writeSecondary(b, s, *c.secondary, to_tape);
		}
		// Stored only on a wrap, so the frame does not load and store the flag.
		if (wrapped) {
			s.wrapped = true;
		}

		StereoSample out = dry;
		out += feedback;
		return out;
	}

	/// @brief The high-pass's memory to 0 in both channels, as a new delay's is. The impulse response's history is
	/// kept.
	[[gnu::always_inline]] static void reset_high_pass(State& s) {
		FeedbackChain::reset_part<1>(s.left);
		FeedbackChain::reset_part<1>(s.right);
	}

private:
	/// @brief Writes @p sample (Q31) to the growing @p secondary, clearing ahead of its read head and counting its
	/// steps into `secondary_written`: natively one step then a write at its write head, resampled the steps its
	/// advance takes then the triangle its strengths give.
	/// @return Whether its read head wrapped.
	[[nodiscard]] [[gnu::always_inline]] static bool writeSecondary(const Block& b, State& s, DelayTape& secondary,
	                                                                StereoSample sample) {
		bool wrapped = false;
		if (!b.secondary_resampled) {
			wrapped = secondary.clearAndMoveOn();
			++s.secondary_written;
			secondary.writeNative(sample);
			return wrapped;
		}
		const int32_t strength2 = secondary.advance([&] [[gnu::always_inline]] {
			wrapped = secondary.clearAndMoveOn() || wrapped;
			++s.secondary_written;
		});
		secondary.writeResampled(sample, 65536 - strength2, strength2);
		return wrapped;
	}
};

static_assert(processor_kernel<Delay>);
static_assert(dsp::detail::state_in_place<Delay>);
static_assert(!std::is_polymorphic_v<Delay>);

} // namespace deluge::dsp::fixed
