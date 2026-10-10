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

#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/delay/delay_tape.h"
#include "dsp-next/fixed/delay/legacy/analog_impulse_response.h"

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

/// @brief One block of the delay over tapes it is handed: read (silence while the primary has nothing), shape the
/// feedback, high-pass it, mix it into the block (ping-pong or straight), and write it back to both tapes.
///
/// The primary tape is read from the read head, clearing as it goes, and written back from the same head, kWriteLag
/// samples behind. A secondary tape (a new size being grown while the rate settles) is written alongside, clearing
/// ahead of its own head; the owner swaps it in once it is full. The processor owns the feedback's state: the analog
/// impulse response and the post high-pass's memories.
class DelayProcessor {
public:
	/// @brief Renders one block: the delayed signal is added to @p block (Q31), and the block plus the shaped
	/// feedback is written back to the tapes.
	/// @param primary the tape read and written back; null or inactive reads silence and writes nothing.
	/// @param secondary null when the delay has a single tape.
	/// @param scratch the working block, distinct from @p block and at least as long.
	[[nodiscard]] DelayBlockResult process(std::span<StereoSample> block, DelayTape* primary, DelayTape* secondary,
	                                       const DelayBlock& settings, std::span<StereoSample> scratch);

	/// @brief The post high-pass's memories to 0, as a new delay's are.
	void resetHighPass() {
		high_pass_l_ = 0;
		high_pass_r_ = 0;
	}

private:
	/// @brief Reads @p primary into @p work from its read head, clearing as it goes.
	/// @return Whether the read head wrapped.
	static bool read(DelayTape& primary, std::span<StereoSample> work);

	/// @brief Scales @p work (Q31) by the feedback amount and saturates it: the analog impulse response then tanh, or
	/// a digital clip at 29 bits. Either way the result is shifted back up by 2.
	void shapeFeedback(std::span<StereoSample> work, const DelayBlock& settings);

	/// @brief Takes a one-pole low-pass (a 1/2048 step per sample) out of @p work (Q31), so repeats don't build up
	/// sub-bass. The corner settles near 40 Hz after many repeats.
	void highPass(std::span<StereoSample> work);

	/// @brief Adds @p work (the delayed signal) to @p block, and @p block to @p work for the write: straight, or
	/// crossed for ping-pong when the engine renders in stereo (the delayed right feeds the left; the delayed left
	/// plus the mean of the block's channels feeds the right).
	static void mix(std::span<StereoSample> work, std::span<StereoSample> block, bool cross);

	/// @brief Writes @p work back to @p primary from @p head, its read head before the read.
	static void writePrimary(DelayTape& primary, DelayTape::Head head, std::span<const StereoSample> work);

	/// @brief Writes @p work to @p secondary, clearing ahead of its read head.
	/// @return Whether its read head wrapped, and how many steps it took.
	static DelayBlockResult writeSecondary(DelayTape& secondary, std::span<const StereoSample> work);

	legacy::AnalogImpulseResponse analog_ir_;
	/// The high-pass's low-passed left level, Q31.
	int32_t high_pass_l_ = 0;
	/// The high-pass's low-passed right level, Q31.
	int32_t high_pass_r_ = 0;
};

} // namespace deluge::dsp::fixed
