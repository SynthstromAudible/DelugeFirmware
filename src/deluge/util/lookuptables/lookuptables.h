/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

// NOTE: the preset *name* / iterance tables (presetReverbNames, chordNames, iterancePresets) that need
// gui/l10n + model/iterance live in util/lookuptables/preset_tables.h, so this header stays a leaf of
// numeric DSP tables (no gui/ or model/ reach). Include preset_tables.h if you need those.

// ALIGNMENT: every table DEFINITION below carries `alignas(16)` (see the .cpp files). The
// oscillator render path reads these through NEON/argon lane loads, which require aligned memory,
// but a plain `const int16_t[]` is only 2-byte-aligned by default. Because the tables are an odd
// number of samples long, consecutive tables land at alternating alignments and roughly half end
// up 2 (mod 4) — an unaligned lane load can fault on device, and even when it doesn't, unaligned
// NEON loads cost extra cycles on Cortex-A9 and can straddle a cache line.
//
// The specifier lives on the definitions only, deliberately. [dcl.align] permits a non-defining
// declaration to carry no alignment-specifier, so these externs need no annotation and cannot
// drift out of agreement with the definitions.
extern const uint16_t centAdjustTableSmall[];
extern const int16_t hanningWindow[];

// Begins at E (4 semitones above C). So that this octave contains the largest values (phase increments) possible
// without going over 22.05kHz (2147483648), even when shifted up a semitone (via osc-cents and unison combined)
extern const int32_t noteFrequencyTable[12];
extern const int32_t noteIntervalTable[12];
extern const int32_t attackRateTable[51];
extern const int32_t releaseRateTable[51];
extern const int32_t releaseRateTable64[65];

#define NUM_PRESET_REVERBS 3
const uint8_t presetReverbRoomSize[NUM_PRESET_REVERBS] = {16, 30, 44};
const uint8_t presetReverbDamping[NUM_PRESET_REVERBS] = {29, 36, 45};
// presetReverbNames -> util/lookuptables/preset_tables.h (needs l10n::String)

extern const uint8_t noteCodeToNoteLetter[];
extern const uint8_t noteCodeToNoteLetterFlats[];
extern const bool noteCodeIsSharp[];

// iterancePresets -> util/lookuptables/preset_tables.h (needs Iterance)

#define MAX_CHORD_TYPES 9
#define MAX_CHORD_NOTES 4
extern uint8_t chordTypeSemitoneOffsets[MAX_CHORD_TYPES][MAX_CHORD_NOTES];
extern uint8_t chordTypeNoteCount[MAX_CHORD_TYPES];
// chordNames -> util/lookuptables/preset_tables.h (needs l10n::String)
