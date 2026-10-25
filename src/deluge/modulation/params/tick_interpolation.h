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

#include <cstdint>

/// Params which can't interpolate per sample (MIDI, CV) interpolate per tick instead, but no more often than this
/// (~1ms)
constexpr int32_t kMinSamplesBetweenTickInterpolations = 44;

/// Ticks til the next per-tick interpolation update. 0 (come back next tick) at normal tempos, but at very fast ones a
/// tick is only a few samples long - and each update makes the audio routine stop rendering there, so updating every
/// tick would render audio a few samples at a time and the CPU couldn't keep up (#5106)
constexpr int32_t ticksTilNextTickInterpolation(uint32_t samplesPerTick) {
	if (samplesPerTick >= kMinSamplesBetweenTickInterpolations) {
		return 0;
	}
	if (samplesPerTick == 0) {
		samplesPerTick = 1;
	}
	return (kMinSamplesBetweenTickInterpolations + samplesPerTick - 1) / samplesPerTick;
}
