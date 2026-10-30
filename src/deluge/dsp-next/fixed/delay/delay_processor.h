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

#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/delay/delay_kernel.h"
#include "dsp-next/fixed/delay/delay_tape.h"
#include "dsp-next/fixed/delay/feedback_shaper.h"

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

/// @brief One block of the delay over tapes it is handed, rendered in place as one fused frame loop (@ref Delay): each
/// frame reads the primary tape (silence while the primary has nothing), shapes and high-passes the feedback, mixes it
/// into the block (ping-pong or straight), and writes it back to both tapes before the next frame is read, so an echo
/// shorter than the block comes round within it as the delay line plays it.
///
/// The primary tape is read from the read head, clearing as it goes, and written back from the same head, kWriteLag
/// samples behind. A secondary tape (a new size being grown while the rate settles) is written alongside, clearing
/// ahead of its own head; the owner swaps it in once it is full.
///
/// **Feedback.** Each channel's delayed sample goes through a @ref FeedbackShaper (the feedback amount and the
/// saturation: the analog impulse response then a tanh, or a digital clip) and then a high-pass that takes a one-pole
/// low-pass (a 1/2048 step per sample) out of it, so repeats don't build up sub-bass; the corner settles near 40 Hz
/// after many repeats. The processor owns the chain's state in the kernel's State: each channel's impulse-response
/// history and high-pass memory, the history 50 words and an index per channel, mirrored so its tap loop stores
/// nothing. Every ModControllableAudio (a Sound, a kit drum, a kit, an audio clip) carries one through its Delay, and
/// a kit drum is allocated by `alloc_fast`, which tries the 44 KB internal SRAM heap first.
///
/// **Where it is built.** The kernel is instantiated only in this component's source, which builds with the DSP
/// library's flags, so the steady-state frame loops make no out-of-line call (the one cold call is taken only while a
/// resampled secondary grows); the app's translation units see only the declaration.
///
/// **Cost.** Per channel-sample on the device at 128-frame blocks: about 32 cycles native digital, 67 to 114 resampled
/// digital, 189 native analog, where the response's two tap loops dominate (docs/dev/a9_dsp_performance.md, Delay).
///
/// **Reactivation.** @ref reset_high_pass resets the high-pass's memories only. The impulse response's history carries
/// over, so a delay switched off and on again starts with up to 25 samples of the old response's tail. The render gate
/// `render-audio-track-delay-toggle` pins it.
///
/// **Mode switches.** A switch between analog and digital between blocks keeps the response's history too (see
/// @ref FeedbackShaper): analog resumes from the last analog block's. `feedback_shaper_spec`, `delay_processor_spec`
/// and the render gate `render-audio-track-delay-switch` pin it.
class DelayProcessor {
public:
	/// @brief The feedback's stages, Q31: shaped by the amount and saturated, then high-passed.
	using FeedbackChain = DelayFeedbackChain;

	/// @brief Renders one block in place: the delayed signal is added to @p block (Q31), and the block plus the shaped
	/// feedback is written back to the tapes, frame by frame.
	/// @details The kernel is configured from @p settings and the tapes right before the block, since its tape form
	/// and whether it reads and grows come from the tapes' state, which the owner's lifecycle changes between blocks.
	/// @param block The frames to process in place, Q31.
	/// @param primary The tape read and written back; null or inactive reads silence and writes nothing.
	/// @param secondary Null when the delay has a single tape.
	/// @param settings The block's settings as the owner resolved them.
	/// @return What the block did to the tapes.
	[[nodiscard]] DelayBlockResult process(std::span<StereoSample> block, DelayTape* primary, DelayTape* secondary,
	                                       const DelayBlock& settings);

	/// @brief The high-pass's memories to 0 in both channels, as a new delay's are. The impulse response's history
	/// is kept.
	void reset_high_pass();

	/// @brief The kernel's State between blocks.
	/// @return Each channel's impulse response history and high-pass memory. Its block results are cleared after every
	/// block.
	[[nodiscard]] const Delay::State& state() const { return delay_.state(); }

private:
	/// @brief The fused frame kernel, its feedback memories in its State.
	Mono<Delay> delay_;
};

} // namespace deluge::dsp::fixed
