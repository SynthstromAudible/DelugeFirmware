/*
 * Copyright © 2015-2026 Synthstrom Audible Limited
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
#include "dsp-next/core/stereo.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/delay/delay_tape.h"
#include "dsp-next/fixed/delay/feedback_shaper.h"
#include "dsp-next/fixed/filter/shift_tracker.h"

#include <cstdint>
#include <span>

namespace deluge::dsp::fixed {

/// @brief One block's delay settings as the owner resolved them.
struct DelayBlock {
	int32_t rate;              ///< the tape speed this block (the owner's resolved user rate; 16777216 is neutral)
	int32_t feedback;          ///< the feedback amount, Q31
	bool analog;               ///< analog: the impulse response then tanh at analog_saturation; else digital clipping
	int32_t analog_saturation; ///< the tanh drive, as a power of two: 8 on a sound, 5 on a host chain
	bool ping_pong;            ///< the feedback crosses between the channels
	bool render_in_stereo;     ///< ping-pong crosses channels only when the engine renders in stereo
};

/// @brief What a block did to the tapes, for the owner's lifecycle.
struct DelayBlockResult {
	bool wrapped;              ///< the read head wrapped (the abandon count steps): the secondary tape's when it is
	                           ///< active, else the primary's
	int32_t secondary_written; ///< tape samples, not block samples: the secondary tape's read-head steps this block
	                           ///< (sizeLeftUntilBufferSwap's step), two per block sample at twice its native rate
};

/// @brief One block of the delay over tapes it is handed: read (silence while the primary has nothing), shape and
/// high-pass the feedback in one pass, mix it into the block (ping-pong or straight), and write it back to both tapes.
///
/// The primary tape is read from the read head, clearing as it goes, and written back from the same head, kWriteLag
/// samples behind. A secondary tape (a new size being grown while the rate settles) is written alongside, clearing
/// ahead of its own head; the owner swaps it in once it is full.
///
/// **Feedback.** Each channel's delayed sample goes through a @ref FeedbackShaper (the feedback amount and the
/// saturation: the analog impulse response then a tanh, or a digital clip) and then a high-pass that takes a one-pole
/// low-pass (a 1/2048 step per sample) out of it, so repeats don't build up sub-bass; the corner settles near 40 Hz
/// after many repeats. Both run as one chain, every sample through both stages in a single pass over the block, the
/// mode chosen once per block. The processor owns the chain's state: each channel's impulse-response history and
/// high-pass memory, 436 bytes in all, against 208 for a response that keeps 25 partial sums: the history is 50 words
/// and an index per channel, mirrored so its tap loop stores nothing. Every ModControllableAudio (a Sound, a kit drum,
/// a kit, an audio clip) carries one through its Delay, and a kit drum is allocated by `alloc_fast`, which tries the
/// 44 KB internal SRAM heap first.
///
/// **Reactivation.** @ref resetHighPass resets the high-pass's memories only. The impulse response's history carries
/// over, so a delay switched off and on again starts with up to 25 samples of the old response's tail. The render gate
/// `render-audio-track-delay-toggle` pins it.
///
/// **Mode switches.** A switch between analog and digital between blocks keeps the response's history too (see
/// @ref FeedbackShaper): analog resumes from the last analog block's. `feedback_shaper_spec`, `delay_processor_spec`
/// and the render gate `render-audio-track-delay-switch` pin it.
class DelayProcessor {
public:
	/// @brief Renders one block: the delayed signal is added to @p block (Q31), and the block plus the shaped
	/// feedback is written back to the tapes.
	/// @param primary the tape read and written back; null or inactive reads silence and writes nothing.
	/// @param secondary null when the delay has a single tape.
	/// @param scratch the working block, distinct from @p block and at least as long.
	[[nodiscard]] DelayBlockResult process(std::span<StereoSample> block, DelayTape* primary, DelayTape* secondary,
	                                       const DelayBlock& settings, std::span<StereoSample> scratch);

	/// @brief The high-pass's memories to 0 in both channels, as a new delay's are. The impulse response's history
	/// is kept.
	void resetHighPass() { feedback_.on<&FeedbackChain::reset_part<1>>(); }

private:
	/// @brief The feedback's stages, Q31: shaped by the amount and saturated, then high-passed.
	using FeedbackChain = Chain<FeedbackShaper, Pinned<ShiftTracker<11>, ShiftTracker<11>::Mode::REMOVE>>;

	/// @brief Reads @p primary into @p work from its read head, clearing as it goes.
	/// @return Whether the read head wrapped.
	static bool read(DelayTape& primary, std::span<StereoSample> work);

	/// @brief Adds @p work (the delayed signal) to @p block, and @p block to @p work for the write: straight, or
	/// crossed for ping-pong when the engine renders in stereo (the delayed right feeds the left; the delayed left
	/// plus the mean of the block's channels feeds the right).
	static void mix(std::span<StereoSample> work, std::span<StereoSample> block, bool cross);

	/// @brief Writes @p work back to @p primary from @p head, its read head before the read.
	static void writePrimary(DelayTape& primary, DelayTape::Head head, std::span<const StereoSample> work);

	/// @brief Writes @p work to @p secondary, clearing ahead of its read head.
	/// @return Whether its read head wrapped, and how many steps it took.
	static DelayBlockResult writeSecondary(DelayTape& secondary, std::span<const StereoSample> work);

	/// @brief The feedback chain on both channels: the impulse response's history and the high-pass's memory per
	/// channel.
	Stereo<FeedbackChain> feedback_;
};

} // namespace deluge::dsp::fixed
