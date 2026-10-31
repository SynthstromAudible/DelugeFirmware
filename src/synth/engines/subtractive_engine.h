/*
 * Copyright © 2014-2023 Synthstrom Audible Limited, 2026 Synthstrom Audible Limited
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

#include "synth/engines/dual_source_engine.h"
#include <array>
#include <cstdint>

namespace deluge::synth {

/// @brief The subtractive engine: both sources mixed, with source 1 optionally hard-synced to source 0, and the noise
/// source, filtered by the voice afterwards. It renders the mono sources first, then any stereo ones over a stereo
/// copy of them.
class SubtractiveEngine : public DualSourceEngine {
public:
	/// Renders @p block's sources and noise, adding into its buffer, and stores the levels the next block starts from.
	static EngineRenderResult render(const DualSourceBlock& block);

private:
	/// What each source's render in this block shares.
	struct Pass {
		std::array<int32_t, kNumSources> amplitudes{};            ///< Each source's amplitude at block start
		std::array<int32_t, kNumSources> amplitude_increments{};  ///< Its per-sample increment
		std::array<int32_t, kNumSources> wave_index_increments{}; ///< Each wavetable index's per-sample slope
		std::array<uint32_t, kMaxNumVoicesUnison> sync_pos{};     ///< The osc-sync master's phase in each part
		/// The master's increment in each part, which source 1 syncs to; 0 (too high: the slave skips) where the
		/// master did not render the part.
		std::array<uint32_t, kMaxNumVoicesUnison> sync_increments{};
	};

	/// The block's levels (held, or ramping from the last block's), wavetable slopes and osc-sync master phases, from
	/// this block's @p source_amplitudes.
	static Pass begin(const DualSourceBlock& block, const std::array<int32_t, kNumSources>& source_amplitudes);
	/// Renders source @p s into the mono (or, with @p stereo, the stereo) buffer: source 0 takes the osc-sync master's
	/// increments even when silent, and source 1 syncs to them. @return whether a unison part became inactive.
	static bool render_one(const DualSourceBlock& block, Pass& pass, int32_t s, bool stereo);
};

} // namespace deluge::synth
