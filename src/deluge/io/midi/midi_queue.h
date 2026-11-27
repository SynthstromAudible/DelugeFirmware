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

#include "definitions_cxx.hpp"
#include <cstdint>

/// Output times are microseconds on the deluge_clock_now() clock, truncated to 32 bits. That wraps every 71 minutes,
/// so they are only ever compared by signed difference, never by magnitude.
using OutputTime = uint32_t;

/// The low 32 bits of deluge_clock_now(): the clock output times are kept on.
OutputTime outputTimeNow();

/// Microseconds per audio sample, in Q8 fixed point
constexpr uint32_t kMicrosPerSampleQ8 = ((1'000'000u << 8) + kSampleRate / 2) / kSampleRate;

/// The time the playhead reaches `offset` samples into a render window, given the time the window began rendering and
/// how many samples from then until the playhead played the window's first sample
OutputTime outputTimeAtWindowOffset(OutputTime windowStart, int32_t samplesUntilWindowPlays, int32_t offset);
