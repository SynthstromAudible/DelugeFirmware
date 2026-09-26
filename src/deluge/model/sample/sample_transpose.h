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

namespace deluge::sample {

/// @brief How far to transpose a sample so its detected note plays at middle C's key, split into whole semitones
///        and cents.
struct SampleTranspose {
	int32_t semitones;
	int32_t cents;
};

/// @brief The transpose that brings a sample detected at @p midiNote to middle C (60). PURE.
/// @param midiNote               The detected note, fractional. The caller has checked it is neither unset nor an
/// error.
/// @param minimizeOctaves        The sample is its source's only range: fold the semitones into -5..6.
/// @param rangeCoversJustOneNote The sample's range covers a single key: a detection within 4 cents of that key is
///                               snapped to it, so the file plays exactly in tune there.
/// @param thatOneNote            That key, as a MIDI note.
[[nodiscard]] SampleTranspose transposeForNote(float midiNote, bool minimizeOctaves, bool rangeCoversJustOneNote,
                                               int32_t thatOneNote);

} // namespace deluge::sample
