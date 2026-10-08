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
#include "dsp-next/fixed/delay/delay_tape.h"
#include "dsp/stereo_sample.h"
#include <cstdint>

/// @brief A delay tape's storage, taken from the app's heap: the delay's and the stutterer's buffers.
///
/// The buffer owns the storage and hands out the tape that plays it. Copying a buffer copies the tape's view of the
/// same storage; the copy owns it once the original is invalidated.
class DelayBuffer {
public:
	DelayBuffer() = default;
	~DelayBuffer() { discard(); }

	/// @brief Allocates a tape for a delay at @p rate (1 is 16777216): its ideal size, with kWriteLag samples more for
	/// the resampled write's run-off when @p includeExtraSpace.
	/// @return Error::UNSPECIFIED, leaving the buffer as it was, when the ideal size is @p failIfThisSize;
	/// Error::INSUFFICIENT_RAM, leaving the buffer without storage, when the heap has no room.
	Error init(uint32_t rate, uint32_t failIfThisSize = 0, bool includeExtraSpace = true);

	/// @brief Forgets the storage without freeing it: another buffer, copied from this one, owns it now.
	constexpr void invalidate() { tape_ = {}; }

	/// @brief Frees the storage, if any.
	void discard();

	/// @return The tape over this buffer's storage; inactive when there is none.
	[[nodiscard]] constexpr deluge::dsp::fixed::DelayTape& tape() { return tape_; }
	/// @return The tape over this buffer's storage; inactive when there is none.
	[[nodiscard]] constexpr const deluge::dsp::fixed::DelayTape& tape() const { return tape_; }

private:
	deluge::dsp::fixed::DelayTape tape_;
};
