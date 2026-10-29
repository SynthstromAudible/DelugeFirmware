/*
 * Copyright © 2026 Mark Adams
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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::dsp {

/// JP-8000 style supersaw: a centre saw plus up to six detuned side saws, band limited with polyBLEP. Spread sets both
/// the detune (quadratically) and the side/centre mix.
class Supersaw {
public:
	static constexpr size_t kNumSaws = 7;

	/// Phases of the side saws. The centre saw's phase is the regular oscillator phase.
	using SidePhases = std::array<uint32_t, kNumSaws - 1>;

	/// Side phases spread around the centre phase, so a retriggered supersaw always starts the same way and the saws
	/// don't start out in phase with each other. Each is another golden ratio of a cycle on from the last.
	static constexpr SidePhases sidePhasesFrom(uint32_t centrePhase) {
		SidePhases phases{};
		for (size_t i = 0; i < phases.size(); i++) {
			phases[i] = centrePhase + static_cast<uint32_t>(i + 1) * 0x9E3779B9u;
		}
		return phases;
	}

	/// Pulse width takes saws away: all of them at zero, down to just the centre saw at full. Negative values (from
	/// patching) count the same as positive, as they do for the other oscillators' pulse width.
	static constexpr int32_t numSawsFromPulseWidth(uint32_t pulseWidth) {
		uint32_t pulseWidthAbsolute = (static_cast<int32_t>(pulseWidth) >= 0) ? pulseWidth : -pulseWidth;
		auto removed = static_cast<int32_t>((static_cast<uint64_t>(pulseWidthAbsolute) * kNumSaws) >> 31);
		return kNumSaws - std::min<int32_t>(removed, kNumSaws - 1);
	}

	/// Render, overwriting the buffer, at the same RMS level as a single saw with a peak of 2^29. Writes in blocks of
	/// 4, so the buffer must have room for numSamples rounded up to a multiple of 4.
	/// @param spread Bipolar, +-2^30, as the wave index param
	/// @param numSaws 1 to kNumSaws. Saws are added as the centre, then pairs from the outside in
	/// @param bandLimit Whether to apply the polyBLEP. Without it the saws alias, but it saves CPU when that's dire
	static void render(int32_t* __restrict__ buffer, int32_t numSamples, uint32_t phaseIncrement, int32_t spread,
	                   int32_t numSaws, uint32_t& centrePhase, SidePhases& sidePhases, bool bandLimit);
};

} // namespace deluge::dsp
