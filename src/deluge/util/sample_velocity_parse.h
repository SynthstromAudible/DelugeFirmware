/*
 * Copyright (c) 2024-2026 Synthstrom Audible Limited
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

#include "storage/multi_range/multisample_range.h"
#include <cstdint>

/// Scans `filename` (just the file's own name, not a full path) for the last case-insensitive
/// occurrence of "vel" - allowing it to be followed by more letters, so "velocity" matches too -
/// followed (after skipping any run of '_', '-' or ' ' separators) by 1-3 digits forming a value in
/// [1, 127]. On a match, writes the value to *outVelocity and returns true; otherwise returns false
/// and leaves *outVelocity untouched.
///
/// Deliberately requires the "vel" marker rather than a bare digit run or a bare "v" prefix, since a
/// bare "v" collides with common version-tag conventions ("Kick_v2.wav" meaning "version 2", not
/// velocity 2). Examples that match: "Kick_C1_vel100.wav" -> 100, "Snare-VEL64.wav" -> 64,
/// "HatVelocity1.wav" -> 1. Examples that don't: "Kick_v2.wav" (no "vel"), "Tom_vel200.wav" (200 is
/// out of Deluge's real 1-127 velocity range), "Ride_velvet.wav" (letters follow "vel", digits never
/// found before the next non-digit).
bool tryParseVelocityFromFilename(char const* filename, uint8_t* outVelocity);

/// Given `count` (1..kMaxRoundRobinAlternates + 1) velocity values - one per round-robin slot, in
/// slot order - fills outMin/outMax (each `count` entries, indexed the same as `velocities`) with a
/// partition of the full 1-127 range: sorted ascending by value, with boundaries at the midpoint
/// between each consecutive pair (rounded down, the slot below keeps the boundary value itself), so
/// the resulting ranges are contiguous and non-overlapping regardless of the input order. Ties are
/// broken by giving the boundary to the lower-indexed (in sorted order) of the two - still produces
/// a valid, non-overlapping partition, just with one of the tied slots effectively unreachable.
///
/// Used to turn folder-import velocity tags into round-robin slot ranges (see
/// MultisampleRange::setVelocityRange()). `count` <= 1 is a no-op (nothing to partition).
void computeVelocityLayerBoundaries(const uint8_t* velocities, int32_t count, uint8_t* outMin, uint8_t* outMax);
