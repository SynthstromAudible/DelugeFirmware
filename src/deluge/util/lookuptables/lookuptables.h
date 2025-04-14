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

#include "gui/l10n/strings.h"
#include "model/iterance/iterance.h"
#include <array>
#include <cstdint>

extern const uint16_t centAdjustTableSmall[];
extern const uint16_t decayTableSmall4[];
extern const uint16_t decayTableSmall8[];
extern const uint16_t expTableSmall[];
extern const int16_t tanHSmall[];
extern const int16_t tanH2d[][129];
extern const int16_t hanningWindow[];

extern std::array<const int16_t, 257> sineWaveSmall;

extern std::array<const int16_t, 513> sawWave1;
extern std::array<const int16_t, 513> sawWave3;
extern std::array<const int16_t, 1025> sawWave5;
extern std::array<const int16_t, 1025> sawWave7;
extern std::array<const int16_t, 1025> sawWave9;
extern std::array<const int16_t, 1025> sawWave13;
extern std::array<const int16_t, 2049> sawWave19;
extern std::array<const int16_t, 2049> sawWave27;
extern std::array<const int16_t, 2049> sawWave39;
extern std::array<const int16_t, 2049> sawWave53;
extern std::array<const int16_t, 2049> sawWave76;
extern std::array<const int16_t, 2049> sawWave109;
extern std::array<const int16_t, 2049> sawWave153;
extern std::array<const int16_t, 2049> sawWave215;

extern std::array<const int16_t, 513> squareWave1;
extern std::array<const int16_t, 513> squareWave3;
extern std::array<const int16_t, 1025> squareWave5;
extern std::array<const int16_t, 1025> squareWave7;
extern std::array<const int16_t, 1025> squareWave9;
extern std::array<const int16_t, 1025> squareWave13;
extern std::array<const int16_t, 2049> squareWave19;
extern std::array<const int16_t, 2049> squareWave27;
extern std::array<const int16_t, 2049> squareWave39;
extern std::array<const int16_t, 2049> squareWave53;
extern std::array<const int16_t, 2049> squareWave76;
extern std::array<const int16_t, 2049> squareWave109;
extern std::array<const int16_t, 2049> squareWave153;
extern std::array<const int16_t, 2049> squareWave215;

extern std::array<const int16_t, 2049> mysterySynthASaw_153;
extern std::array<const int16_t, 2049> mysterySynthASaw_215;
extern std::array<const int16_t, 2049> mysterySynthASaw_305;
extern std::array<const int16_t, 2049> mysterySynthASaw_431;
extern std::array<const int16_t, 2049> mysterySynthASaw_609;
extern std::array<const int16_t, 4097> mysterySynthASaw_861;
extern std::array<const int16_t, 4097> mysterySynthASaw_1217;
extern std::array<const int16_t, 8193> mysterySynthASaw_1722;

extern std::array<const int16_t, 513> mysterySynthBSaw_1;
extern std::array<const int16_t, 513> mysterySynthBSaw_3;
extern std::array<const int16_t, 1025> mysterySynthBSaw_5;
extern std::array<const int16_t, 1025> mysterySynthBSaw_7;
extern std::array<const int16_t, 1025> mysterySynthBSaw_9;
extern std::array<const int16_t, 1025> mysterySynthBSaw_13;
extern std::array<const int16_t, 2049> mysterySynthBSaw_19;
extern std::array<const int16_t, 2049> mysterySynthBSaw_27;
extern std::array<const int16_t, 2049> mysterySynthBSaw_39;
extern std::array<const int16_t, 2049> mysterySynthBSaw_53;
extern std::array<const int16_t, 2049> mysterySynthBSaw_76;
extern std::array<const int16_t, 2049> mysterySynthBSaw_109;

extern std::array<const int16_t, 513> analogSquare_1;
extern std::array<const int16_t, 513> analogSquare_3;
extern std::array<const int16_t, 1025> analogSquare_5;
extern std::array<const int16_t, 1025> analogSquare_7;
extern std::array<const int16_t, 1025> analogSquare_9;
extern std::array<const int16_t, 1025> analogSquare_13;
extern std::array<const int16_t, 2049> analogSquare_19;
extern std::array<const int16_t, 2049> analogSquare_27;
extern std::array<const int16_t, 2049> analogSquare_39;
extern std::array<const int16_t, 2049> analogSquare_53;
extern std::array<const int16_t, 2049> analogSquare_76;
extern std::array<const int16_t, 2049> analogSquare_109;
extern std::array<const int16_t, 2049> analogSquare_153;
extern std::array<const int16_t, 2049> analogSquare_215;
extern std::array<const int16_t, 2049> analogSquare_305;
extern std::array<const int16_t, 2049> analogSquare_431;
extern std::array<const int16_t, 2049> analogSquare_609;
extern std::array<const int16_t, 4097> analogSquare_861;
extern std::array<const int16_t, 4097> analogSquare_1217;
extern std::array<const int16_t, 8193> analogSquare_1722;

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
extern deluge::l10n::String presetReverbNames[NUM_PRESET_REVERBS];

extern const uint8_t noteCodeToNoteLetter[];
extern const bool noteCodeIsSharp[];

extern const std::array<Iterance, 35> iterancePresets;

#define MAX_CHORD_TYPES 9
#define MAX_CHORD_NOTES 4
extern uint8_t chordTypeSemitoneOffsets[MAX_CHORD_TYPES][MAX_CHORD_NOTES];
extern uint8_t chordTypeNoteCount[MAX_CHORD_TYPES];
extern const std::array<deluge::l10n::String, MAX_CHORD_TYPES> chordNames;
