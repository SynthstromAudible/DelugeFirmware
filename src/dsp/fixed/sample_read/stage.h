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

#include "definitions_cxx.hpp" // kInterpolationMaxNumSamples
#include "dsp/fixed/interpolation/interpolator.h"
#include "dsp/fixed/sample_read/frame_source.h"

#include <array>
#include <cstdint>

namespace deluge::dsp::fixed::sample_read {

/// @file
/// @brief The sample read's staging: a run's frames as planar int16 taps, newest first, after the tap window's
/// history, so a read's interpolation reads each output's taps straight from memory instead of sliding a window.
///
/// The sliding window at any moment is the last 16 entries of one sequence: the window as the read found it, then
/// every frame fed. Staging lays that sequence out descending in memory: the history in the top 16 slots, verbatim
/// (slot `kStageTaps − 16 + k` holds window tap k, newest at k = 0), and each decoded frame in the slot below the one
/// before. Sixteen slots from an output's newest tap are then its window, in the windowed-sinc table's order.

/// @brief The window's taps, carried in staging's top slots.
inline constexpr int32_t kHistoryTaps = kInterpolationMaxNumSamples;
/// @brief Staging's slots per channel, the history included.
inline constexpr int32_t kStageTaps = 1024;

/// @brief Planar int16 taps per channel, newest first. The engine owns one; no two reads use it at once.
struct Staging {
	alignas(16) std::array<int16_t, kStageTaps> l{};
	alignas(16) std::array<int16_t, kStageTaps> r{};
};

/// @brief Decodes @p frames frames of @p run from its frame @p from into @p l and @p r newest first: run frame
/// `from + f` goes to slot `frames − 1 − f`, each channel its sample's `top_word` (the top halfword of the 32-bit read
/// ending at its top byte; at depth 1 the byte before the sample is the low byte). @p r is unused, and may be null,
/// for a mono run. Reads only the bytes of the frames it decodes, and at depth 1 the byte before the first.
void decode_taps(const FrameRun& run, int32_t from, int32_t frames, int16_t* l, int16_t* r);

/// @brief Copies @p window's taps into @p s's history slots, the right channel's when @p channels is 2.
void load_history(Staging& s, const Interpolator& window, int32_t channels);

/// @brief Writes @p window's taps 0 to @p slots − 1 from @p s's slots from @p newest on: 16 after a sinc read, 2 after
/// a linear one, which leaves the window's other taps as they were.
void store_history(const Staging& s, int32_t newest, Interpolator& window, int32_t channels, int32_t slots);

} // namespace deluge::dsp::fixed::sample_read
