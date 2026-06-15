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

extern const uint16_t centAdjustTableSmall[];
extern const uint16_t decayTableSmall4[];
extern const uint16_t decayTableSmall8[];
extern const uint16_t expTableSmall[];
extern const int16_t tanHSmall[];
extern const int16_t tanH2d[][129];
extern const int16_t hanningWindow[];

extern const int16_t sineWaveSmall[];

// The band-limited oscillator wave LUTs (triangle AA, saw, square, analog saw/square) moved
// into their dsp-next kernels (dsp-next/fixed/{saw,square,triangle,analog_saw,analog_square}.cpp),
// which own them internally. sineWaveSmall stays here (read by the dsp-next sine_table evaluator + the
// wavetable engine).

// Begins at E (4 semitones above C). So that this octave contains the largest values (phase increments) possible
// without going over 22.05kHz (2147483648), even when shifted up a semitone (via osc-cents and unison combined)
extern const int32_t noteFrequencyTable[12];
extern const int32_t noteIntervalTable[12];
extern const int32_t timeStretchAdjustTable[193];
extern const int32_t attackRateTable[51];
extern const int32_t releaseRateTable[51];
extern const int32_t releaseRateTable64[65];
extern const int32_t tanTable[65];
extern const int16_t oldResonanceCompensation[];

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
