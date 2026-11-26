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

#include <array>
#include <cstdint>

/// Takes the internal speaker's sound back out of the internal mic, for mic velocity.
///
/// Measured on a Deluge (2026-11): the speaker reaches the mic 2.5 ms after the codec is given it, and above ~250 Hz
/// what the mic hears of it is close to linear in what was sent (coherence 0.84-0.94). An NLMS filter on the
/// decimated (5.5 kHz) signals learns that path and subtracts its estimate, which took 6-8 dB of a loud song out of
/// the band thumps are measured in. It mustn't learn while a pad is thumping (the thump isn't in what was sent, so it
/// would learn to cancel noise), so the caller freezes it around presses.
namespace deluge::dsp::mic_velocity {

class EchoCanceller {
public:
	/// Filter length and how far back it starts, in decimated samples (5.5 kHz): it covers 0.7-12 ms after a frame
	/// was sent, around the measured 2.5 ms
	static constexpr int32_t kTaps = 64;
	static constexpr int32_t kDelay = 4;

	void reset();

	/// One decimated sample: what the mic heard, and what was sent to the codec for the same moment. Returns the mic
	/// with the speaker's estimated part taken out. adapt: whether to learn from this sample.
	float process(float mic, float sent, bool adapt);

	/// How much the filter has learnt: the sum of its squared weights (0 when reset)
	[[nodiscard]] float weightEnergy() const;

private:
	static constexpr int32_t kHistory = kTaps + kDelay;
	// Sent samples, twice over so the newest kHistory are always contiguous for the filter
	std::array<float, kHistory * 2> sent_{};
	int32_t pos_ = 0;
	std::array<float, kTaps> weights_{};
	float power_ = 0; // Sum of the squares of the sent samples under the filter
	int32_t sinceRecompute_ = 0;
};

} // namespace deluge::dsp::mic_velocity
